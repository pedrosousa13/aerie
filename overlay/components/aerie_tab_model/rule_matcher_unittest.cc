// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_tab_model/rule_matcher.h"

#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace aerie_tab_model {

namespace {

Rule MakeRule(RuleType type, const std::string& value, bool enabled = true) {
  Rule rule;
  rule.id = "r1";
  rule.type = type;
  rule.value = value;
  rule.enabled = enabled;
  return rule;
}

// A Domain rule as the editor and the loader store it: normalised once.
bool DomainMatches(const std::string& domain, const std::string& url) {
  return Matches(MakeRule(RuleType::kDomain, NormalizeDomain(domain)), GURL(url),
                 "");
}

}  // namespace

TEST(RuleMatcherTest, DomainMatchesTheHostItself) {
  EXPECT_TRUE(DomainMatches("example.com", "https://example.com/"));
  EXPECT_TRUE(DomainMatches("example.com", "http://example.com/a?b=c#d"));
  EXPECT_TRUE(DomainMatches("example.com", "https://example.com:8443/x"));
}

TEST(RuleMatcherTest, DomainMatchesAnySubdomain) {
  EXPECT_TRUE(DomainMatches("example.com", "https://news.example.com/"));
  EXPECT_TRUE(DomainMatches("example.com", "https://a.b.example.com/"));
}

TEST(RuleMatcherTest, DomainDoesNotMatchAHostThatOnlyEndsTheSame) {
  EXPECT_FALSE(DomainMatches("example.com", "https://badexample.com/"));
  EXPECT_FALSE(DomainMatches("example.com", "https://example.com.evil.test/"));
  EXPECT_FALSE(DomainMatches("example.com", "https://example.org/"));
  EXPECT_FALSE(DomainMatches("news.example.com", "https://example.com/"));
}

TEST(RuleMatcherTest, DomainIgnoresCaseAndTypingHabits) {
  EXPECT_TRUE(DomainMatches("Example.COM", "https://example.com/"));
  EXPECT_TRUE(DomainMatches("  example.com  ", "https://example.com/"));
  EXPECT_TRUE(DomainMatches("*.example.com", "https://example.com/"));
  EXPECT_TRUE(DomainMatches(".example.com", "https://news.example.com/"));
  EXPECT_TRUE(DomainMatches("example.com.", "https://example.com/"));
  // A pasted address keeps only its host.
  EXPECT_TRUE(
      DomainMatches("https://example.com/path?x=1", "https://news.example.com/"));
  EXPECT_TRUE(DomainMatches("example.com:8080", "https://example.com/"));
}

TEST(RuleMatcherTest, AnIpv4HostMatchesOnlyExactly) {
  EXPECT_TRUE(DomainMatches("1.2.3.4", "http://1.2.3.4/"));
  EXPECT_TRUE(DomainMatches("1.2.3.4", "http://1.2.3.4:8080/x"));
  EXPECT_FALSE(DomainMatches("3.4", "http://1.2.3.4/"));
  EXPECT_FALSE(DomainMatches("2.3.4", "http://1.2.3.4/"));
  // The raw strings, as the one function that compares them sees them.
  EXPECT_FALSE(HostMatchesDomain("1.2.3.4", "3.4"));
  EXPECT_FALSE(HostMatchesDomain("1.2.3.4", "2.3.4"));
  EXPECT_TRUE(HostMatchesDomain("1.2.3.4", "1.2.3.4"));
  // Not as the tail of a name either.
  EXPECT_FALSE(HostMatchesDomain("x.1.2.3.4", "1.2.3.4"));
}

TEST(RuleMatcherTest, AnIpv6HostMatchesOnlyExactly) {
  EXPECT_TRUE(DomainMatches("[::1]", "http://[::1]:8000/"));
  EXPECT_TRUE(DomainMatches("::1", "http://[::1]/"));
  EXPECT_FALSE(DomainMatches("[::2]", "http://[::1]/"));
  EXPECT_FALSE(HostMatchesDomain("[2001:db8::1]", "[db8::1]"));
  EXPECT_FALSE(HostMatchesDomain("[2001:db8::1]", "::1]"));
  EXPECT_TRUE(HostMatchesDomain("[2001:db8::1]", "[2001:db8::1]"));
}

TEST(RuleMatcherTest, NormalizeDomainIsWhatTheMatcherExpects) {
  EXPECT_EQ("example.com", NormalizeDomain("  *.Example.COM. "));
  EXPECT_EQ("1.2.3.4", NormalizeDomain("http://1.2.3.4:80/x"));
  EXPECT_EQ("[::1]", NormalizeDomain("::1"));
}

TEST(RuleMatcherTest, DomainHandlesInternationalHosts) {
  EXPECT_TRUE(DomainMatches("münchen.de", "https://www.münchen.de/"));
  EXPECT_TRUE(DomainMatches("xn--mnchen-3ya.de", "https://www.münchen.de/"));
}

TEST(RuleMatcherTest, EmptyOrBrokenDomainNeverMatches) {
  EXPECT_FALSE(DomainMatches("", "https://example.com/"));
  EXPECT_FALSE(DomainMatches("   ", "https://example.com/"));
  EXPECT_FALSE(DomainMatches("example.com", ""));
  EXPECT_FALSE(DomainMatches("example.com", "not a url"));
  EXPECT_FALSE(DomainMatches("example.com", "about:blank"));
}

TEST(RuleMatcherTest, UrlStartsWithIsAPrefix) {
  Rule rule = MakeRule(RuleType::kUrlStartsWith, "https://github.com/aerie");
  EXPECT_TRUE(Matches(rule, GURL("https://github.com/aerie"), ""));
  EXPECT_TRUE(Matches(rule, GURL("https://github.com/aerie/issues/1"), ""));
  EXPECT_TRUE(Matches(rule, GURL("https://github.com/aerie-tools"), ""));
  EXPECT_FALSE(Matches(rule, GURL("https://github.com/other/aerie"), ""));
  EXPECT_FALSE(Matches(rule, GURL("https://gitlab.com/aerie"), ""));
}

TEST(RuleMatcherTest, UrlStartsWithNormalisesSchemeAndTrailingSlash) {
  // http and https are the same page for a rule.
  Rule https = MakeRule(RuleType::kUrlStartsWith, "https://example.com/docs/");
  EXPECT_TRUE(Matches(https, GURL("http://example.com/docs/a"), ""));
  EXPECT_TRUE(Matches(https, GURL("https://example.com/docs"), ""));
  // The rule may leave the scheme out.
  Rule bare = MakeRule(RuleType::kUrlStartsWith, "example.com/docs");
  EXPECT_TRUE(Matches(bare, GURL("https://example.com/docs/a"), ""));
  // Host case is not significant.
  Rule upper = MakeRule(RuleType::kUrlStartsWith, "HTTPS://EXAMPLE.com/docs");
  EXPECT_TRUE(Matches(upper, GURL("https://example.com/docs/a"), ""));
  // Another scheme is another page.
  Rule file = MakeRule(RuleType::kUrlStartsWith, "file:///tmp/");
  EXPECT_TRUE(Matches(file, GURL("file:///tmp/a.html"), ""));
  EXPECT_FALSE(Matches(file, GURL("https://tmp/a.html"), ""));
}

TEST(RuleMatcherTest, UrlStartsWithASiteEndsAtTheHost) {
  Rule rule = MakeRule(RuleType::kUrlStartsWith, "example.com");
  EXPECT_TRUE(Matches(rule, GURL("https://example.com/"), ""));
  EXPECT_TRUE(Matches(rule, GURL("https://example.com/a"), ""));
  EXPECT_TRUE(Matches(rule, GURL("https://example.com:8080/a"), ""));
  EXPECT_TRUE(Matches(rule, GURL("https://example.com/?q=1"), ""));
  EXPECT_FALSE(Matches(rule, GURL("https://example.com.evil.test/"), ""));
  EXPECT_FALSE(Matches(rule, GURL("https://example.community/"), ""));
}

TEST(RuleMatcherTest, UrlContainsFindsTheTextAnywhere) {
  Rule rule = MakeRule(RuleType::kUrlContains, "/jira/");
  EXPECT_TRUE(Matches(rule, GURL("https://corp.example.com/jira/browse/X-1"), ""));
  EXPECT_FALSE(Matches(rule, GURL("https://corp.example.com/wiki/"), ""));
  Rule query = MakeRule(RuleType::kUrlContains, "utm_source=news");
  EXPECT_TRUE(Matches(query, GURL("https://example.com/?utm_source=News"), ""));
}

TEST(RuleMatcherTest, UrlContainsIgnoresCase) {
  Rule rule = MakeRule(RuleType::kUrlContains, "JIRA");
  EXPECT_TRUE(Matches(rule, GURL("https://example.com/jira/x"), ""));
}

TEST(RuleMatcherTest, TitleContainsIgnoresCase) {
  Rule rule = MakeRule(RuleType::kTitleContains, "Trip Planner");
  EXPECT_TRUE(Matches(rule, GURL("https://example.com/"), "my TRIP planner - Docs"));
  EXPECT_TRUE(Matches(rule, GURL("https://example.com/"), "trip planner"));
  EXPECT_FALSE(Matches(rule, GURL("https://example.com/"), "Planner of trips"));
  EXPECT_FALSE(Matches(rule, GURL("https://example.com/"), ""));
}

TEST(RuleMatcherTest, TitleContainsFoldsNonAsciiLetters) {
  Rule rule = MakeRule(RuleType::kTitleContains, "ÜBER");
  EXPECT_TRUE(Matches(rule, GURL("https://example.com/"), "Alles über uns"));
}

TEST(RuleMatcherTest, TitleRuleDoesNotNeedAUrl) {
  Rule rule = MakeRule(RuleType::kTitleContains, "docs");
  EXPECT_TRUE(Matches(rule, GURL(), "My docs"));
}

TEST(RuleMatcherTest, AnEmptyValueNeverMatches) {
  for (RuleType type : {RuleType::kDomain, RuleType::kUrlStartsWith,
                        RuleType::kUrlContains, RuleType::kTitleContains}) {
    EXPECT_FALSE(Matches(MakeRule(type, ""), GURL("https://example.com/"), "x"));
    EXPECT_FALSE(Matches(MakeRule(type, "  "), GURL("https://example.com/"), "x"));
  }
}

TEST(RuleMatcherTest, TypeNamesRoundTrip) {
  for (RuleType type : {RuleType::kDomain, RuleType::kUrlStartsWith,
                        RuleType::kUrlContains, RuleType::kTitleContains}) {
    RuleType back = RuleType::kDomain;
    ASSERT_TRUE(RuleTypeFromString(RuleTypeToString(type), &back));
    EXPECT_EQ(type, back);
  }
  RuleType ignored;
  EXPECT_FALSE(RuleTypeFromString("regex", &ignored));
}

TEST(FindMatchingRuleTest, FirstEnabledMatchingRuleWins) {
  FolderRules rules;
  Rule a = MakeRule(RuleType::kDomain, "nothing.test");
  a.id = "a";
  Rule b = MakeRule(RuleType::kDomain, "example.com");
  b.id = "b";
  Rule c = MakeRule(RuleType::kUrlContains, "example");
  c.id = "c";
  rules.rules = {a, b, c};
  const Rule* found =
      FindMatchingRule(rules, GURL("https://example.com/"), "", "tab");
  ASSERT_TRUE(found);
  EXPECT_EQ("b", found->id);
}

TEST(FindMatchingRuleTest, ADisabledRuleIsSkipped) {
  FolderRules rules;
  Rule off = MakeRule(RuleType::kDomain, "example.com", /*enabled=*/false);
  off.id = "off";
  Rule on = MakeRule(RuleType::kUrlContains, "example");
  on.id = "on";
  rules.rules = {off, on};
  const Rule* found =
      FindMatchingRule(rules, GURL("https://example.com/"), "", "tab");
  ASSERT_TRUE(found);
  EXPECT_EQ("on", found->id);
  rules.rules = {off};
  EXPECT_FALSE(FindMatchingRule(rules, GURL("https://example.com/"), "", "tab"));
}

TEST(FindMatchingRuleTest, ADisabledFolderMatchesNothing) {
  FolderRules rules;
  rules.rules = {MakeRule(RuleType::kDomain, "example.com")};
  rules.enabled = false;
  EXPECT_FALSE(FindMatchingRule(rules, GURL("https://example.com/"), "", "tab"));
}

TEST(FindMatchingRuleTest, ANoRuleFolderMatchesNothing) {
  FolderRules rules;
  EXPECT_FALSE(FindMatchingRule(rules, GURL("https://example.com/"), "t", "tab"));
}

TEST(FindMatchingRuleTest, AnExcludedHostStopsTheFolder) {
  FolderRules rules;
  rules.rules = {MakeRule(RuleType::kUrlContains, "example")};
  rules.excluded_hosts = {"news.example.com"};
  EXPECT_FALSE(
      FindMatchingRule(rules, GURL("https://news.example.com/a"), "", "tab"));
  // A subdomain of the excluded host is excluded too.
  EXPECT_FALSE(
      FindMatchingRule(rules, GURL("https://x.news.example.com/a"), "", "tab"));
  // The parent site and a lookalike are not.
  EXPECT_TRUE(FindMatchingRule(rules, GURL("https://example.com/a"), "", "tab"));
  EXPECT_TRUE(
      FindMatchingRule(rules, GURL("https://badnews.example.com/a"), "", "tab"));
}

TEST(FindMatchingRuleTest, AnExcludedTabStopsTheFolderForThatTabOnly) {
  FolderRules rules;
  rules.rules = {MakeRule(RuleType::kDomain, "example.com")};
  rules.excluded_tabs = {"tab-1"};
  EXPECT_FALSE(
      FindMatchingRule(rules, GURL("https://example.com/"), "", "tab-1"));
  EXPECT_TRUE(
      FindMatchingRule(rules, GURL("https://example.com/"), "", "tab-2"));
  // An empty tab id never equals an empty exception.
  rules.excluded_tabs = {""};
  EXPECT_TRUE(FindMatchingRule(rules, GURL("https://example.com/"), "", ""));
}

TEST(FindMatchingRuleTest, ATitleRuleStillMatchesWhenTheUrlIsEmpty) {
  FolderRules rules;
  rules.rules = {MakeRule(RuleType::kTitleContains, "docs")};
  rules.excluded_hosts = {"example.com"};
  EXPECT_TRUE(FindMatchingRule(rules, GURL(), "My Docs", "tab"));
}

}  // namespace aerie_tab_model
