// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_fingerprint/site_key.h"

#include <optional>
#include <string>

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

TEST(NoiseSitePatternTest, ARegistrableDomainGetsASubdomainPattern) {
  EXPECT_EQ(NoiseSitePattern(GURL("https://www.example.com/page")),
            std::optional<std::string>("[*.]example.com"));
  EXPECT_EQ(NoiseSitePattern(GURL("http://x.a.localhost:4100/")),
            std::optional<std::string>("[*.]a.localhost"));
  EXPECT_EQ(NoiseSitePattern(GURL("https://alice.github.io/")),
            std::optional<std::string>("[*.]alice.github.io"));
}

TEST(NoiseSitePatternTest, AHostWithoutARegistrableDomainIsExact) {
  EXPECT_EQ(NoiseSitePattern(GURL("http://localhost:3000/")),
            std::optional<std::string>("localhost"));
  EXPECT_EQ(NoiseSitePattern(GURL("http://192.168.1.5/")),
            std::optional<std::string>("192.168.1.5"));
}

TEST(NoiseSitePatternTest, OnlyWebAddressesHaveOne) {
  EXPECT_FALSE(NoiseSitePattern(GURL("file:///tmp/a.html")).has_value());
  EXPECT_FALSE(NoiseSitePattern(GURL("chrome://settings/")).has_value());
  EXPECT_FALSE(NoiseSitePattern(GURL()).has_value());
}

}  // namespace

}  // namespace aerie_fingerprint
