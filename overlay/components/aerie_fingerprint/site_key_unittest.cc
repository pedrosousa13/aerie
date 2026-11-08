// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_fingerprint/site_key.h"

#include <string>

#include "components/content_settings/core/common/content_settings_pattern.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace aerie_fingerprint {

namespace {

url::Origin O(const char* spec) {
  return url::Origin::Create(GURL(spec));
}

TEST(NoiseSiteKeyTest, HostsOfOneSiteShareAKey) {
  const std::string key = NoiseSiteKey(O("https://example.com/"));
  EXPECT_FALSE(key.empty());
  EXPECT_EQ(key, NoiseSiteKey(O("https://www.example.com/")));
  EXPECT_EQ(key, NoiseSiteKey(O("https://a.b.example.com/x")));
  EXPECT_EQ(key, NoiseSiteKey(O("https://example.com:8443/")));
}

TEST(NoiseSiteKeyTest, LocalhostSubdomainsAreOneSite) {
  EXPECT_EQ(NoiseSiteKey(O("http://x.a.localhost:4100/")),
            NoiseSiteKey(O("http://y.a.localhost:4100/")));
  EXPECT_EQ(NoiseSiteKey(O("http://x.a.localhost:4100/")),
            NoiseSiteKey(O("http://a.localhost:4100/")));
}

TEST(NoiseSiteKeyTest, DifferentSitesGetDifferentKeys) {
  EXPECT_NE(NoiseSiteKey(O("http://a.localhost/")),
            NoiseSiteKey(O("http://b.localhost/")));
  EXPECT_NE(NoiseSiteKey(O("https://example.com/")),
            NoiseSiteKey(O("https://example.org/")));
}

TEST(NoiseSiteKeyTest, ThePublicSuffixListSeparatesSites) {
  // github.io is a private registry: each user is a site.
  EXPECT_NE(NoiseSiteKey(O("https://alice.github.io/")),
            NoiseSiteKey(O("https://bob.github.io/")));
  EXPECT_NE(NoiseSiteKey(O("https://a.co.uk/")),
            NoiseSiteKey(O("https://b.co.uk/")));
}

TEST(NoiseSiteKeyTest, TheSchemeIsPartOfTheSite) {
  EXPECT_NE(NoiseSiteKey(O("http://example.com/")),
            NoiseSiteKey(O("https://example.com/")));
}

TEST(NoiseSiteKeyTest, AnOpaqueOriginHasNoKey) {
  EXPECT_TRUE(NoiseSiteKey(url::Origin()).empty());
  EXPECT_TRUE(NoiseSiteKey(O("data:text/html,hi")).empty());
}

TEST(NoiseSiteKeyTest, APartitionComponentSeparatesTheSameSite) {
  const url::Origin origin = O("https://example.com/");
  EXPECT_EQ(NoiseSiteKey(origin), NoiseSiteKey(origin, ""));
  EXPECT_NE(NoiseSiteKey(origin), NoiseSiteKey(origin, "workspace-1"));
  EXPECT_NE(NoiseSiteKey(origin, "workspace-1"),
            NoiseSiteKey(origin, "workspace-2"));
  EXPECT_EQ(NoiseSiteKey(origin, "workspace-1"),
            NoiseSiteKey(O("https://www.example.com/"), "workspace-1"));
}

TEST(NoiseSitePatternTest, ARegistrableDomainGetsASchemeAndSubdomainPattern) {
  EXPECT_EQ(NoiseSitePattern(GURL("https://www.example.com/page")).ToString(),
            "https://[*.]example.com");
  EXPECT_EQ(NoiseSitePattern(GURL("http://x.a.localhost:4100/")).ToString(),
            "http://[*.]a.localhost");
  EXPECT_EQ(NoiseSitePattern(GURL("https://alice.github.io/")).ToString(),
            "https://[*.]alice.github.io");
}

TEST(NoiseSitePatternTest, AHostWithoutARegistrableDomainIsExactWithItsScheme) {
  EXPECT_EQ(NoiseSitePattern(GURL("http://localhost:3000/")).ToString(),
            "http://localhost");
  EXPECT_EQ(NoiseSitePattern(GURL("http://192.168.1.5/")).ToString(),
            "http://192.168.1.5");
  EXPECT_EQ(NoiseSitePattern(GURL("https://192.168.1.5/")).ToString(),
            "https://192.168.1.5");
}

TEST(NoiseSitePatternTest, ThePatternCoversExactlyTheHostsOfTheKeysSite) {
  const GURL base("https://www.example.com/");
  const ContentSettingsPattern pattern = NoiseSitePattern(base);
  for (const char* same : {"https://example.com/", "https://a.b.example.com/x",
                           "https://example.com:8443/"}) {
    EXPECT_TRUE(pattern.Matches(GURL(same))) << same;
    EXPECT_EQ(NoiseSiteKey(O(same)), NoiseSiteKey(O(base.spec().c_str())))
        << same;
  }
  // The scheme is part of the key, so it is part of the pattern.
  EXPECT_FALSE(pattern.Matches(GURL("http://example.com/")));
  EXPECT_NE(NoiseSiteKey(O("http://example.com/")),
            NoiseSiteKey(O("https://example.com/")));
  EXPECT_FALSE(pattern.Matches(GURL("https://example.org/")));
  EXPECT_FALSE(pattern.Matches(GURL("https://notexample.com/")));
}

TEST(NoiseSitePatternTest, OnlyWebAddressesHaveOne) {
  EXPECT_FALSE(NoiseSitePattern(GURL("file:///tmp/a.html")).IsValid());
  EXPECT_FALSE(NoiseSitePattern(GURL("chrome://settings/")).IsValid());
  EXPECT_FALSE(NoiseSitePattern(GURL()).IsValid());
}

}  // namespace

}  // namespace aerie_fingerprint
