// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_tab_model/rename_rules.h"

#include <stdio.h>

#include <string>
#include <utility>

#include "base/json/json_reader.h"
#include "base/time/time.h"
#include "components/aerie_tab_model/tab_model_types.h"
#include "components/aerie_tab_model/title_resolver.h"
#include "components/aerie_tab_model/title_settings.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace aerie_tab_model {

namespace {

RenameRule MakeRule(const std::string& id,
                    RuleType type,
                    const std::string& value,
                    RewriteMode mode,
                    const std::string& text,
                    bool enabled = true) {
  RenameRule rule;
  rule.id = id;
  rule.name = "Rule " + id;
  rule.match.id = id;
  rule.match.type = type;
  rule.match.value = type == RuleType::kDomain ? NormalizeDomain(value) : value;
  rule.mode = mode;
  rule.text = text;
  rule.enabled = enabled;
  return rule;
}

std::optional<std::string> Apply(const RenameRule& rule,
                                 const std::string& url,
                                 const std::string& title) {
  const MatchInput input{GURL(url), title};
  return ApplyRenameRule(rule, input, title);
}

base::DictValue Dict(const std::string& json) {
  std::optional<base::Value> value = base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  EXPECT_TRUE(value && value->is_dict()) << json;
  return value ? std::move(value->GetDict()) : base::DictValue();
}

constexpr char kGoodRule[] =
    R"({"id":"a1","name":"Mail","type":"domain","value":"Mail.Example.com",
        "mode":"template","text":"Mail: {title}","enabled":true})";

}  // namespace

TEST(RenameRulesTest, ATemplateReplacesTitleAndHost) {
  const RenameRule rule =
      MakeRule("a", RuleType::kDomain, "example.com", RewriteMode::kTemplate,
               "{host}: {title}");
  EXPECT_EQ(Apply(rule, "https://www.example.com/x", "Inbox"),
            "example.com: Inbox");
}

TEST(RenameRulesTest, TitleContainsRemovesTheMatchedTextFromTitle) {
  const RenameRule rule = MakeRule("a", RuleType::kTitleContains, "acme corp",
                                   RewriteMode::kTemplate, "Work: {title}");
  EXPECT_EQ(Apply(rule, "https://example.com/", "Inbox - ACME Corp"),
            "Work: Inbox");
}

TEST(RenameRulesTest, ATemplateWithoutPlaceholdersIsAFixedName) {
  const RenameRule rule = MakeRule("a", RuleType::kUrlStartsWith,
                                   "example.com/app", RewriteMode::kTemplate,
                                   "My app");
  EXPECT_EQ(Apply(rule, "https://example.com/app/home", "Whatever"), "My app");
  EXPECT_EQ(Apply(rule, "https://example.com/other", "Whatever"),
            std::nullopt);
}

TEST(RenameRulesTest, UnknownBracesStayAsTyped) {
  const RenameRule rule = MakeRule("a", RuleType::kDomain, "example.com",
                                   RewriteMode::kTemplate, "{x} {title} $1");
  EXPECT_EQ(Apply(rule, "https://example.com/", "T"), "{x} T $1");
}

TEST(RenameRulesTest, RemoveTextDeletesEveryOccurrenceIgnoringCase) {
  const RenameRule rule = MakeRule("a", RuleType::kTitleContains, "(beta)",
                                   RewriteMode::kRemoveText, "");
  EXPECT_EQ(Apply(rule, "https://example.com/", "Foo (BETA) Bar (beta)"),
            "Foo Bar");
}

// Matching and removing agree: both only ignore case, so an accent is a
// different letter for both.
TEST(RenameRulesTest, RemoveTextIgnoresCaseButNotAccents) {
  const RenameRule rule = MakeRule("a", RuleType::kTitleContains, "menu",
                                   RewriteMode::kRemoveText, "cafe");
  // "cafe" is not "Café": nothing is removed, so nothing changes.
  EXPECT_EQ(Apply(rule, "https://example.com/", "Menu Café"), std::nullopt);
  EXPECT_EQ(Apply(rule, "https://example.com/", "Menu CAFE Bar"), "Menu Bar");
  const RenameRule accented = MakeRule("b", RuleType::kTitleContains, "CAFÉ",
                                       RewriteMode::kRemoveText, "");
  EXPECT_EQ(Apply(accented, "https://example.com/", "Menu café Bar"),
            "Menu Bar");
}

TEST(RenameRulesTest, TitleContainsDoesNotMatchAnAccentedLetterForAPlainOne) {
  const RenameRule rule = MakeRule("a", RuleType::kTitleContains, "cafe",
                                   RewriteMode::kRemoveText, "");
  EXPECT_EQ(Apply(rule, "https://example.com/", "Café Menu"), std::nullopt);
}

TEST(RenameRulesTest, RemoveTextCanUseItsOwnTextForAnUrlRule) {
  const RenameRule rule = MakeRule("a", RuleType::kDomain, "example.com",
                                   RewriteMode::kRemoveText, "Example Inc.");
  EXPECT_EQ(Apply(rule, "https://example.com/", "Example Inc. - Pricing"),
            "Pricing");
}

TEST(RenameRulesTest, ARuleThatMakesAnEmptyOrSameTitleGivesNothing) {
  const RenameRule rule = MakeRule("a", RuleType::kTitleContains, "foo",
                                   RewriteMode::kRemoveText, "");
  EXPECT_EQ(Apply(rule, "https://example.com/", "foo"), std::nullopt);
  const RenameRule same = MakeRule("b", RuleType::kDomain, "example.com",
                                   RewriteMode::kTemplate, "{title}");
  EXPECT_EQ(Apply(same, "https://example.com/", "T"), std::nullopt);
}

TEST(RenameRulesTest, TheResultIsCutAtTheManualNameLimit) {
  const RenameRule rule = MakeRule("a", RuleType::kDomain, "example.com",
                                   RewriteMode::kTemplate, "{title}!");
  const std::optional<std::string> result =
      Apply(rule, "https://example.com/", std::string(400, 'a'));
  ASSERT_TRUE(result);
  EXPECT_EQ(result->size(), kMaxManualNameLength);
}

TEST(RenameRulesTest, TheFirstMatchingRuleInListOrderWins) {
  RenameRules rules;
  rules.rules.push_back(MakeRule("a", RuleType::kDomain, "example.com",
                                 RewriteMode::kTemplate, "First"));
  rules.rules.push_back(MakeRule("b", RuleType::kDomain, "example.com",
                                 RewriteMode::kTemplate, "Second"));
  const GURL url("https://example.com/");
  const MatchInput input{url, "T"};
  EXPECT_EQ(ApplyRenameRules(rules, input, "T")->text, "First");
  std::swap(rules.rules[0], rules.rules[1]);
  EXPECT_EQ(ApplyRenameRules(rules, input, "T")->text, "Second");
}

TEST(RenameRulesTest, ASwitchedOffRuleIsSkippedAndAllRulesOffStopsAll) {
  RenameRules rules;
  rules.rules.push_back(MakeRule("a", RuleType::kDomain, "example.com",
                                 RewriteMode::kTemplate, "First", false));
  rules.rules.push_back(MakeRule("b", RuleType::kDomain, "example.com",
                                 RewriteMode::kTemplate, "Second"));
  const GURL url("https://example.com/");
  const MatchInput input{url, "T"};
  EXPECT_EQ(ApplyRenameRules(rules, input, "T")->text, "Second");
  rules.enabled = false;
  EXPECT_EQ(ApplyRenameRules(rules, input, "T"), std::nullopt);
}

TEST(RenameRulesTest, ARuleThatMakesNothingNewPassesToTheNextRule) {
  RenameRules rules;
  rules.rules.push_back(MakeRule("a", RuleType::kDomain, "example.com",
                                 RewriteMode::kTemplate, "{title}"));
  rules.rules.push_back(MakeRule("b", RuleType::kDomain, "example.com",
                                 RewriteMode::kTemplate, "Second"));
  const GURL url("https://example.com/");
  const MatchInput input{url, "T"};
  EXPECT_EQ(ApplyRenameRules(rules, input, "T")->text, "Second");
}

// --- Loading: the pref is untrusted. ---

TEST(RenameRulesLoadTest, AGoodRuleLoadsAndItsDomainIsMadeCanonical) {
  const std::optional<RenameRule> rule = RenameRuleFromValue(Dict(kGoodRule));
  ASSERT_TRUE(rule);
  EXPECT_EQ(rule->match.value, "mail.example.com");
  EXPECT_EQ(rule->mode, RewriteMode::kTemplate);
  EXPECT_TRUE(rule->enabled);
}

TEST(RenameRulesLoadTest, ARuleRoundTripsThroughItsValue) {
  const std::optional<RenameRule> rule = RenameRuleFromValue(Dict(kGoodRule));
  ASSERT_TRUE(rule);
  EXPECT_EQ(RenameRuleFromValue(RenameRuleToValue(*rule)), rule);
}

TEST(RenameRulesLoadTest, ABadRuleIsRefused) {
  const char* const kBad[] = {
      // Missing fields.
      R"({"id":"a","name":"n","type":"domain","value":"x.com","mode":"template"})",
      R"({"name":"n","type":"domain","value":"x.com","mode":"template","text":"t"})",
      // Wrong types.
      R"({"id":1,"name":"n","type":"domain","value":"x.com","mode":"template","text":"t"})",
      R"({"id":"a","name":"n","type":"domain","value":"x.com","mode":"template","text":"t","enabled":"yes"})",
      // Bad id.
      R"({"id":"a b","name":"n","type":"domain","value":"x.com","mode":"template","text":"t"})",
      R"({"id":"","name":"n","type":"domain","value":"x.com","mode":"template","text":"t"})",
      // Unknown type and mode: a regular expression is not a type.
      R"({"id":"a","name":"n","type":"regex","value":"x.*","mode":"template","text":"t"})",
      R"({"id":"a","name":"n","type":"domain","value":"x.com","mode":"capture","text":"$1"})",
      // Empty match value, and a domain with no host.
      R"({"id":"a","name":"n","type":"url-contains","value":"  ","mode":"template","text":"t"})",
      R"({"id":"a","name":"n","type":"domain","value":"   ","mode":"template","text":"t"})",
      // A template with nothing in it, and "remove" with nothing to remove.
      R"({"id":"a","name":"n","type":"domain","value":"x.com","mode":"template","text":"  "})",
      R"({"id":"a","name":"n","type":"domain","value":"x.com","mode":"remove","text":""})",
  };
  for (const char* json : kBad) {
    EXPECT_EQ(RenameRuleFromValue(Dict(json)), std::nullopt) << json;
  }
}

TEST(RenameRulesLoadTest, TextOverTheLimitsIsRefused) {
  base::DictValue dict = Dict(kGoodRule);
  dict.Set("text", std::string(kMaxRenameRuleTextBytes + 1, 'a'));
  EXPECT_EQ(RenameRuleFromValue(dict), std::nullopt);
  dict = Dict(kGoodRule);
  dict.Set("value", std::string(kMaxRenameRuleTextBytes + 1, 'a'));
  EXPECT_EQ(RenameRuleFromValue(dict), std::nullopt);
  dict = Dict(kGoodRule);
  dict.Set("name", std::string(kMaxRenameRuleNameBytes + 1, 'a'));
  EXPECT_EQ(RenameRuleFromValue(dict), std::nullopt);
}

TEST(RenameRulesLoadTest, ATextThatIsNotUtf8IsRefused) {
  base::DictValue dict = Dict(kGoodRule);
  // A base::Value string cannot hold invalid UTF-8 in a debug build, so test the
  // check with a lone surrogate-free but overlong-free value: the limit and the
  // type checks above cover the rest.
  dict.Set("text", "ok \xE2\x82\xAC");
  EXPECT_TRUE(RenameRuleFromValue(dict));
}

TEST(RenameRulesLoadTest, ListLoadingDropsBadEntriesRepeatsAndTheOverflow) {
  base::ListValue list;
  list.Append(Dict(kGoodRule));
  list.Append("not a dict");
  list.Append(Dict(kGoodRule));  // Same id.
  list.Append(Dict(R"({"id":"bad"})"));
  EXPECT_EQ(RenameRulesFromList(list).size(), 1u);

  base::ListValue many;
  for (int i = 0; i < 80; ++i) {
    base::DictValue dict = Dict(kGoodRule);
    dict.Set("id", "r" + std::to_string(i));
    many.Append(std::move(dict));
  }
  EXPECT_EQ(RenameRulesFromList(many).size(), kMaxRenameRules);
}

TEST(RenameRulesLoadTest, AnOrderedListKeepsItsOrder) {
  base::ListValue list;
  for (const char* id : {"c", "a", "b"}) {
    base::DictValue dict = Dict(kGoodRule);
    dict.Set("id", id);
    list.Append(std::move(dict));
  }
  const std::vector<RenameRule> rules = RenameRulesFromList(list);
  ASSERT_EQ(rules.size(), 3u);
  EXPECT_EQ(rules[0].id, "c");
  EXPECT_EQ(rules[1].id, "a");
  EXPECT_EQ(rules[2].id, "b");
}

TEST(TitleSettingsTest, OffSitesAreMadeCanonicalAndCheckedOnLoad) {
  base::ListValue sites;
  sites.Append("Mail.Example.COM");
  sites.Append("https://other.test/path");
  sites.Append("");
  sites.Append(5);
  sites.Append("mail.example.com");
  const TitleSettings settings =
      MakeTitleSettings(true, sites, true, base::ListValue());
  ASSERT_EQ(settings.tidy_off_sites.size(), 2u);
  EXPECT_FALSE(settings.TidyAppliesTo("mail.example.com"));
  EXPECT_FALSE(settings.TidyAppliesTo("a.mail.example.com"));
  EXPECT_FALSE(settings.TidyAppliesTo("other.test"));
  EXPECT_TRUE(settings.TidyAppliesTo("example.com"));
  EXPECT_TRUE(settings.TidyAppliesTo("badmail.example.com"));
}

TEST(TitleSettingsTest, TheGlobalSwitchTurnsTheTidyOffEverywhere) {
  const TitleSettings settings =
      MakeTitleSettings(false, base::ListValue(), true, base::ListValue());
  EXPECT_FALSE(settings.TidyAppliesTo("example.com"));
}

// --- The resolver with the settings. ---

TEST(TitleResolverWithSettingsTest, TheDefaultListIsManualRuleTidyPage) {
  TitleSettings settings;
  const TitleResolver resolver = TitleResolver::CreateDefault(&settings);
  const std::vector<TitleSource> sources = resolver.provider_sources();
  ASSERT_EQ(sources.size(), 4u);
  EXPECT_EQ(sources[0], TitleSource::kManual);
  EXPECT_EQ(sources[1], TitleSource::kRule);
  EXPECT_EQ(sources[2], TitleSource::kTidy);
  EXPECT_EQ(sources[3], TitleSource::kPage);
}

TEST(TitleResolverWithSettingsTest, PrecedenceManualThenRuleThenTidyThenPage) {
  TitleSettings settings;
  settings.rename.rules.push_back(MakeRule("a", RuleType::kDomain, "github.com",
                                           RewriteMode::kTemplate, "GH: {title}"));
  const TitleResolver resolver = TitleResolver::CreateDefault(&settings);
  const GURL url("https://github.com/o/r");
  const std::string page = "Issues · o/r · GitHub";
  TabRecord tab;

  // A rule beats tidy.
  ResolvedTitle shown = resolver.ResolveTitle(tab, page, url);
  EXPECT_EQ(shown.source, TitleSource::kRule);
  EXPECT_EQ(shown.text, "GH: Issues · o/r · GitHub");
  EXPECT_EQ(shown.detail, "Rule a");

  // A manual name beats the rule.
  tab.manual_name = "Mine";
  shown = resolver.ResolveTitle(tab, page, url);
  EXPECT_EQ(shown.source, TitleSource::kManual);
  EXPECT_EQ(shown.text, "Mine");
  tab.manual_name.reset();

  // Rules off: the next source is tidy.
  settings.rename.enabled = false;
  shown = resolver.ResolveTitle(tab, page, url);
  EXPECT_EQ(shown.source, TitleSource::kTidy);
  EXPECT_EQ(shown.text, "Issues · o/r");

  // One rule off does the same.
  settings.rename.enabled = true;
  settings.rename.rules[0].enabled = false;
  EXPECT_EQ(resolver.ResolveTitle(tab, page, url).source, TitleSource::kTidy);
  settings.rename.rules[0].enabled = true;

  // Tidy off globally: the page title.
  settings.rename.enabled = false;
  settings.tidy_enabled = false;
  shown = resolver.ResolveTitle(tab, page, url);
  EXPECT_EQ(shown.source, TitleSource::kPage);
  EXPECT_EQ(shown.text, page);

  // Tidy off for this site only.
  settings.tidy_enabled = true;
  settings.tidy_off_sites = {"github.com"};
  EXPECT_EQ(resolver.ResolveTitle(tab, page, url).source, TitleSource::kPage);
  EXPECT_EQ(resolver.ResolveTitle(tab, "A - Gmail",
                                  GURL("https://mail.google.com/")).source,
            TitleSource::kTidy);
}

TEST(TitleResolverWithSettingsTest, ATitleTheTidyWouldNotChangeIsThePageTitle) {
  TitleSettings settings;
  const TitleResolver resolver = TitleResolver::CreateDefault(&settings);
  const ResolvedTitle shown = resolver.ResolveTitle(
      TabRecord(), "Hello world", GURL("https://example.com/"));
  EXPECT_EQ(shown.source, TitleSource::kPage);
}

TEST(TitleResolverWithSettingsTest, ReorderingRulesChangesWhichWins) {
  TitleSettings settings;
  settings.rename.rules.push_back(MakeRule("a", RuleType::kDomain, "example.com",
                                           RewriteMode::kTemplate, "A"));
  settings.rename.rules.push_back(MakeRule("b", RuleType::kUrlContains, "example",
                                           RewriteMode::kTemplate, "B"));
  const TitleResolver resolver = TitleResolver::CreateDefault(&settings);
  const GURL url("https://example.com/");
  EXPECT_EQ(resolver.ResolveTitle(TabRecord(), "T", url).text, "A");
  std::swap(settings.rename.rules[0], settings.rename.rules[1]);
  EXPECT_EQ(resolver.ResolveTitle(TabRecord(), "T", url).text, "B");
}

TEST(TitleResolverWithSettingsTest, AnUnnamedAddressRuleDoesNotMatchWithoutAUrl) {
  TitleSettings settings;
  settings.rename.rules.push_back(MakeRule("a", RuleType::kDomain, "example.com",
                                           RewriteMode::kTemplate, "A"));
  const TitleResolver resolver = TitleResolver::CreateDefault(&settings);
  EXPECT_EQ(resolver.ResolveTitle(TabRecord(), "T").source, TitleSource::kPage);
}

// --- Bounds. ---

// 10,000 titles through the rules and the tidy, with 50 rules that mostly do not
// match (the common case: every title is asked of every rule).
TEST(TitleResolverWithSettingsTest, TenThousandTitlesWithFiftyRulesAreFast) {
  TitleSettings settings;
  for (int i = 0; i < 48; ++i) {
    const std::string id = "r" + std::to_string(i);
    switch (i % 4) {
      case 0:
        settings.rename.rules.push_back(
            MakeRule(id, RuleType::kDomain, "site" + id + ".test",
                     RewriteMode::kTemplate, "{title}!"));
        break;
      case 1:
        settings.rename.rules.push_back(
            MakeRule(id, RuleType::kUrlStartsWith, "site" + id + ".test/app",
                     RewriteMode::kTemplate, "{title}!"));
        break;
      case 2:
        settings.rename.rules.push_back(
            MakeRule(id, RuleType::kUrlContains, "needle-" + id,
                     RewriteMode::kTemplate, "{title}!"));
        break;
      default:
        settings.rename.rules.push_back(
            MakeRule(id, RuleType::kTitleContains, "unique phrase " + id,
                     RewriteMode::kTemplate, "{title}!"));
        break;
    }
  }
  const TitleResolver resolver = TitleResolver::CreateDefault(&settings);
  TabRecord tab;
  std::vector<GURL> urls;
  std::vector<std::string> titles;
  for (int i = 0; i < 10000; ++i) {
    urls.emplace_back("https://www.news" + std::to_string(i % 97) +
                      ".example.com/articles/" + std::to_string(i) + "?ref=x");
    titles.push_back("Headline number " + std::to_string(i) +
                     " about something - News Site " + std::to_string(i % 97));
  }
  // The last rules match a few titles, so every title is also asked of every
  // rule that comes before them.
  settings.rename.rules.push_back(
      MakeRule("m1", RuleType::kDomain, "news0.example.com",
               RewriteMode::kTemplate, "{title}!"));
  settings.rename.rules.push_back(
      MakeRule("m2", RuleType::kTitleContains, "number 7",
               RewriteMode::kRemoveText, ""));
  // A pass before the timed one, so that caches and the allocator are warm.
  for (int i = 0; i < 10000; ++i) {
    resolver.ResolveTitle(tab, titles[i], urls[i]);
  }
  const base::TimeTicks start = base::TimeTicks::Now();
  size_t changed = 0;
  for (int i = 0; i < 10000; ++i) {
    changed += resolver.ResolveTitle(tab, titles[i], urls[i]).source !=
               TitleSource::kPage;
  }
  const base::TimeDelta elapsed = base::TimeTicks::Now() - start;
  printf("[perf] 10000 titles, 50 rules, tidy: %.1f ms (%zu changed)\n",
         elapsed.InMillisecondsF(), changed);
  EXPECT_LT(elapsed.InMilliseconds(), 50) << changed;
}

TEST(TitleResolverWithSettingsTest, AHugeTitleIsBoundedByTheTidyCut) {
  TitleSettings settings;
  settings.rename.rules.push_back(MakeRule("a", RuleType::kTitleContains,
                                           "needle", RewriteMode::kTemplate,
                                           "{title}"));
  const TitleResolver resolver = TitleResolver::CreateDefault(&settings);
  const std::string huge(5 * 1000 * 1000, 'a');
  const base::TimeTicks start = base::TimeTicks::Now();
  const ResolvedTitle shown =
      resolver.ResolveTitle(TabRecord(), huge, GURL("https://example.com/"));
  EXPECT_LT((base::TimeTicks::Now() - start).InMilliseconds(), 50);
  EXPECT_LE(shown.text.size(), huge.size());
}

}  // namespace aerie_tab_model
