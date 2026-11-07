// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_adblock/browser/protection_policy.h"

#include <string>
#include <vector>

#include "components/aerie_adblock/browser/protection_engine.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace aerie_adblock {
namespace {

TEST(ProtectionPolicyTest, OnlyHttpAndHttpsUrlsAreJudged) {
  EXPECT_EQ("evil.example", HostToCheck(GURL("https://Evil.example/login")));
  EXPECT_EQ("evil.example", HostToCheck(GURL("http://evil.example:8080/")));
  EXPECT_EQ("xn--e1afmkfd.example",
            HostToCheck(GURL("https://\xd0\xbf\xd1\x80\xd0\xb8\xd0\xbc\xd0\xb5\xd1\x80.example/")));
  EXPECT_EQ(std::nullopt, HostToCheck(GURL("chrome://settings/")));
  EXPECT_EQ(std::nullopt, HostToCheck(GURL("file:///etc/passwd")));
  EXPECT_EQ(std::nullopt, HostToCheck(GURL("data:text/html,hi")));
  EXPECT_EQ(std::nullopt, HostToCheck(GURL("about:blank")));
  EXPECT_EQ(std::nullopt, HostToCheck(GURL()));
}

TEST(ProtectionPolicyTest, ListedHostGetsTheVerdictOfItsFrameKind) {
  EXPECT_EQ(NavigationVerdict::kShowInterstitial,
            Decide(true, "evil.example", NavigationKind::kOutermostMainFrame, nullptr));
  EXPECT_EQ(NavigationVerdict::kBlockSubframe,
            Decide(true, "evil.example", NavigationKind::kSubframe, nullptr));
  EXPECT_EQ(NavigationVerdict::kCancel,
            Decide(true, "evil.example", NavigationKind::kPrerenderedMainFrame, nullptr));
}

TEST(ProtectionPolicyTest, AnUnlistedHostIsAlwaysAllowed) {
  for (NavigationKind kind :
       {NavigationKind::kOutermostMainFrame, NavigationKind::kSubframe,
        NavigationKind::kPrerenderedMainFrame}) {
    EXPECT_EQ(NavigationVerdict::kAllow, Decide(false, "good.example", kind, nullptr));
  }
}

TEST(ProtectionPolicyTest, ContinueAnywayIsPerHostAndExact) {
  SessionAllowlist allowlist;
  EXPECT_FALSE(allowlist.Contains("a.evil.example"));
  allowlist.Add("a.evil.example");
  allowlist.Add("");  // ignored
  EXPECT_EQ(1u, allowlist.size());
  EXPECT_EQ(NavigationVerdict::kAllow,
            Decide(true, "a.evil.example", NavigationKind::kOutermostMainFrame, &allowlist));
  // A sibling, the parent and a lookalike are not covered.
  for (const char* host : {"b.evil.example", "evil.example", "xa.evil.example"}) {
    EXPECT_EQ(NavigationVerdict::kShowInterstitial,
              Decide(true, host, NavigationKind::kOutermostMainFrame, &allowlist))
        << host;
  }
}

// The Bench and the issue ask for subframe and redirect cases. Every hop of a
// redirect chain is judged on its own with the same function, so a chain is the
// same as a list of navigations. These tests walk chains the way the throttle
// does: engine answer, then Decide() for the hop.
class RedirectChainTest : public testing::Test {
 protected:
  RedirectChainTest() { engine_.LoadListForTesting("phish.example\nmalware.example\n"); }

  // The first hop that is not allowed, or -1 when the whole chain passes.
  int FirstStoppedHop(const std::vector<std::string>& urls, NavigationKind kind,
                      const SessionAllowlist* allowlist = nullptr) {
    for (size_t i = 0; i < urls.size(); ++i) {
      std::optional<std::string> host = HostToCheck(GURL(urls[i]));
      if (!host) {
        continue;
      }
      if (Decide(engine_.IsListed(*host), *host, kind, allowlist) !=
          NavigationVerdict::kAllow) {
        return static_cast<int>(i);
      }
    }
    return -1;
  }

  ProtectionEngine engine_;
};

TEST_F(RedirectChainTest, AListedHostInTheMiddleOfAChainIsCaughtAtItsHop) {
  const std::vector<std::string> chain = {
      "https://short.example/x", "https://tracker.example/r?u=1",
      "https://login.phish.example/", "https://good.example/"};
  EXPECT_EQ(2, FirstStoppedHop(chain, NavigationKind::kOutermostMainFrame));
  EXPECT_EQ(2, FirstStoppedHop(chain, NavigationKind::kSubframe));
}

TEST_F(RedirectChainTest, AChainThatStartsOnAListedHostIsCaughtAtOnce) {
  EXPECT_EQ(0, FirstStoppedHop({"http://malware.example/", "https://good.example/"},
                               NavigationKind::kOutermostMainFrame));
}

TEST_F(RedirectChainTest, ACleanChainPasses) {
  EXPECT_EQ(-1, FirstStoppedHop({"https://a.example/", "https://b.example/"},
                                NavigationKind::kOutermostMainFrame));
}

TEST_F(RedirectChainTest, ContinueAnywayCoversOnlyTheHostTheUserChose) {
  SessionAllowlist allowlist;
  allowlist.Add("login.phish.example");
  const std::vector<std::string> chain = {"https://short.example/",
                                          "https://login.phish.example/",
                                          "https://other.malware.example/"};
  // The first listed host is allowed, the second listed host still stops it.
  EXPECT_EQ(2, FirstStoppedHop(chain, NavigationKind::kOutermostMainFrame, &allowlist));
}

TEST_F(RedirectChainTest, ACaseOrTrailingDotTrickDoesNotHideAListedHost) {
  EXPECT_EQ(0, FirstStoppedHop({"https://LOGIN.Phish.Example/"},
                               NavigationKind::kOutermostMainFrame));
  EXPECT_EQ(0, FirstStoppedHop({"https://login.phish.example./"},
                               NavigationKind::kOutermostMainFrame));
}

}  // namespace
}  // namespace aerie_adblock
