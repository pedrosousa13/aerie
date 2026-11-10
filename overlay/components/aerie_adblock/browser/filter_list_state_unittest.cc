// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_adblock/browser/filter_list_state.h"

#include <map>
#include <optional>
#include <set>
#include <string>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/time/time.h"
#include "base/values.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace aerie_adblock {
namespace {

TEST(FilterListUrlTest, OnlyHttpsIsAcceptable) {
  EXPECT_TRUE(IsAcceptableListUrl(GURL("https://lists.example/a.txt"), false));
  for (const char* bad :
       {"http://lists.example/a.txt", "ftp://lists.example/a.txt",
        "file:///etc/passwd", "data:text/plain,x", "javascript:alert(1)",
        "https://user:pw@lists.example/a.txt", "https://lists.example/a#frag",
        "https:///a.txt", "", "not a url"}) {
    EXPECT_FALSE(IsAcceptableListUrl(GURL(bad), false)) << bad;
  }
}

TEST(FilterListUrlTest, LoopbackHttpNeedsTheTestSwitch) {
  for (const char* loop : {"http://127.0.0.1:8000/a.txt",
                           "http://localhost:8000/a.txt", "http://[::1]/a.txt"}) {
    EXPECT_FALSE(IsAcceptableListUrl(GURL(loop), false)) << loop;
    EXPECT_TRUE(IsAcceptableListUrl(GURL(loop), true)) << loop;
  }
  // The switch never opens http for another host, including look-alikes.
  for (const char* other : {"http://lists.example/a.txt", "http://127.0.0.1.evil.test/a.txt",
                            "http://10.0.0.1/a.txt", "http://192.168.1.1/a.txt"}) {
    EXPECT_FALSE(IsAcceptableListUrl(GURL(other), true)) << other;
  }
}

TEST(FilterListUrlTest, TooLongIsRefused) {
  EXPECT_FALSE(IsAcceptableListUrl(
      GURL("https://lists.example/" + std::string(2100, 'a')), false));
}

TEST(FilterListIdTest, OnlyShortLowercaseAlphanumericIdsAreValid) {
  EXPECT_TRUE(IsValidListId("easylist"));
  EXPECT_TRUE(IsValidListId("c0123456789abcdef"));
  for (const char* bad : {"", "../x", "a/b", "A", "a.b", "a b", "a\\0b"}) {
    EXPECT_FALSE(IsValidListId(bad)) << bad;
  }
  EXPECT_FALSE(IsValidListId(std::string(33, 'a')));
}

TEST(ExceptionHostTest, NormalizesOrRefuses) {
  EXPECT_EQ(NormalizeExceptionHost("News.Test"), "news.test");
  EXPECT_EQ(NormalizeExceptionHost("news.test."), "news.test");
  EXPECT_EQ(NormalizeExceptionHost("127.0.0.1"), "127.0.0.1");
  for (const char* bad : {"", ".", "a b", "a/b", "a@b", "https://a.test", "ä.test",
                          "a..b"}) {
    EXPECT_EQ(NormalizeExceptionHost(bad), "") << bad;
  }
  EXPECT_EQ(NormalizeExceptionHost(std::string(300, 'a')), "");
}

TEST(ExceptionHostTest, MatchesTheHostAndItsSubdomainsOnly) {
  std::set<std::string> ex = {"news.test"};
  EXPECT_TRUE(HostHasException("news.test", ex));
  EXPECT_TRUE(HostHasException("www.news.test", ex));
  EXPECT_TRUE(HostHasException("a.b.news.test", ex));
  EXPECT_FALSE(HostHasException("notnews.test", ex));
  EXPECT_FALSE(HostHasException("news.test.evil.test", ex));
  EXPECT_FALSE(HostHasException("test", ex));
  EXPECT_FALSE(HostHasException("", ex));
}

TEST(FilterListStateTest, StartsWithTheBuiltInListsAndNothingElse) {
  FilterListState state;
  ASSERT_EQ(state.lists().size(), 2u);
  EXPECT_EQ(state.lists()[0].id, "easylist");
  EXPECT_EQ(state.lists()[1].id, "easyprivacy");
  EXPECT_TRUE(state.lists()[0].builtin);
  EXPECT_TRUE(state.auto_update());
  EXPECT_TRUE(state.exceptions().empty());
  EXPECT_FALSE(state.NeedsRebuild());
}

TEST(FilterListStateTest, AddsRemovesAndRejectsCustomLists) {
  FilterListState state;
  std::string id;
  EXPECT_EQ(state.AddCustomList(GURL("https://lists.example/a.txt"), false, &id),
            FilterListState::AddResult::kAdded);
  EXPECT_TRUE(IsValidListId(id));
  EXPECT_TRUE(state.NeedsRebuild());
  EXPECT_EQ(state.AddCustomList(GURL("https://lists.example/a.txt"), false, &id),
            FilterListState::AddResult::kDuplicate);
  EXPECT_EQ(state.AddCustomList(GURL("http://lists.example/b.txt"), false, &id),
            FilterListState::AddResult::kInvalidUrl);
  // A built-in list's address is a duplicate too.
  EXPECT_EQ(state.AddCustomList(GURL(kEasyListUrl), false, &id),
            FilterListState::AddResult::kDuplicate);
  EXPECT_EQ(state.lists().size(), 3u);

  std::string added = state.lists()[2].id;
  EXPECT_FALSE(state.RemoveList("easylist"));
  EXPECT_FALSE(state.RemoveList("nothing"));
  EXPECT_TRUE(state.RemoveList(added));
  EXPECT_EQ(state.lists().size(), 2u);
  EXPECT_FALSE(state.NeedsRebuild());
}

TEST(FilterListStateTest, CustomListsAreCapped) {
  FilterListState state;
  std::string id;
  for (size_t i = 0; i < kMaxCustomLists; ++i) {
    ASSERT_EQ(state.AddCustomList(GURL("https://l.example/" + std::to_string(i)),
                                  false, &id),
              FilterListState::AddResult::kAdded);
  }
  EXPECT_EQ(state.AddCustomList(GURL("https://l.example/one-more"), false, &id),
            FilterListState::AddResult::kTooMany);
}

TEST(FilterListStateTest, ExceptionsAreNormalizedAndCapped) {
  FilterListState state;
  EXPECT_TRUE(state.SetException("News.Test", true));
  EXPECT_EQ(state.exceptions().count("news.test"), 1u);
  EXPECT_FALSE(state.SetException("not a host", true));
  EXPECT_TRUE(state.SetException("news.test", false));
  EXPECT_TRUE(state.exceptions().empty());
  for (size_t i = 0; i < kMaxExceptions; ++i) {
    ASSERT_TRUE(state.SetException("h" + std::to_string(i) + ".test", true));
  }
  EXPECT_FALSE(state.SetException("one-more.test", true));
  EXPECT_TRUE(state.SetException("h1.test", false));  // Removing still works.
}

TEST(FilterListStateTest, CosmeticOffHostsAreNormalizedAndCapped) {
  FilterListState state;
  EXPECT_TRUE(state.cosmetic_off_hosts().empty());
  EXPECT_TRUE(state.SetCosmeticOff("News.Test", true));
  EXPECT_EQ(state.cosmetic_off_hosts().count("news.test"), 1u);
  EXPECT_FALSE(state.SetCosmeticOff("not a host", true));
  // The set is separate from the blocking exceptions.
  EXPECT_TRUE(state.exceptions().empty());
  EXPECT_TRUE(state.SetCosmeticOff("news.test", false));
  EXPECT_TRUE(state.cosmetic_off_hosts().empty());
  for (size_t i = 0; i < kMaxCosmeticOffHosts; ++i) {
    ASSERT_TRUE(state.SetCosmeticOff("h" + std::to_string(i) + ".test", true));
  }
  EXPECT_FALSE(state.SetCosmeticOff("one-more.test", true));
  EXPECT_TRUE(state.SetCosmeticOff("h1.test", true));   // Already in: fine.
  EXPECT_TRUE(state.SetCosmeticOff("h1.test", false));  // Removing still works.
}

TEST(FilterListStateTest, CosmeticOffHostsSurviveASaveAndLoad) {
  FilterListState state;
  state.SetCosmeticOff("a.test", true);
  state.SetCosmeticOff("b.test", true);
  std::optional<base::Value> json =
      base::JSONReader::Read(*base::WriteJson(state.ToValue()), base::JSON_PARSE_RFC);
  ASSERT_TRUE(json && json->is_dict());
  FilterListState back = FilterListState::FromValue(json->GetDict());
  EXPECT_EQ(back.cosmetic_off_hosts(), (std::set<std::string>{"a.test", "b.test"}));
  EXPECT_TRUE(back.exceptions().empty());
}

TEST(FilterListStateTest, HostileCosmeticOffContentFallsBackToSafeValues) {
  // Wrong types for the whole field, then for its items.
  for (const char* text : {R"({"cosmetic_off": "a.test"})", R"({"cosmetic_off": 5})",
                           R"({"cosmetic_off": {"a.test": true}})",
                           R"({"cosmetic_off": null})"}) {
    std::optional<base::Value> json = base::JSONReader::Read(text, base::JSON_PARSE_RFC);
    ASSERT_TRUE(json && json->is_dict()) << text;
    FilterListState state = FilterListState::FromValue(json->GetDict());
    EXPECT_TRUE(state.cosmetic_off_hosts().empty()) << text;
    EXPECT_EQ(state.lists().size(), 2u) << text;
  }
  std::optional<base::Value> json = base::JSONReader::Read(
      R"({"cosmetic_off": ["ok.test", "bad host", 5, null, [], "../x", "OK2.test", ".lead.test"]})",
      base::JSON_PARSE_RFC);
  ASSERT_TRUE(json && json->is_dict());
  EXPECT_EQ(FilterListState::FromValue(json->GetDict()).cosmetic_off_hosts(),
            (std::set<std::string>{"ok.test", "ok2.test"}));
  // A huge string is dropped, not stored.
  base::DictValue huge;
  base::ListValue hosts;
  hosts.Append(std::string(100000, 'a'));
  hosts.Append("fine.test");
  huge.Set("cosmetic_off", std::move(hosts));
  EXPECT_EQ(FilterListState::FromValue(huge).cosmetic_off_hosts(),
            (std::set<std::string>{"fine.test"}));
}

TEST(FilterListStateTest, TooManyCosmeticOffHostsAreCut) {
  base::DictValue dict;
  base::ListValue hosts;
  for (size_t i = 0; i < kMaxCosmeticOffHosts + 500; ++i) {
    hosts.Append("h" + std::to_string(i) + ".test");
  }
  dict.Set("cosmetic_off", std::move(hosts));
  EXPECT_EQ(FilterListState::FromValue(dict).cosmetic_off_hosts().size(),
            kMaxCosmeticOffHosts);
}

TEST(FilterListStateTest, SurvivesASaveAndLoad) {
  FilterListState state;
  std::string id;
  state.AddCustomList(GURL("https://lists.example/a.txt"), false, &id);
  FilterListEntry* entry = state.Find(id);
  entry->title = "Mine";
  entry->rules = 42;
  entry->updated = base::Time::FromSecondsSinceUnixEpoch(1700000000);
  state.Find("easylist")->error = "The list could not be downloaded.";
  state.SetException("news.test", true);
  state.SetAutoUpdate(false);
  state.set_last_update_check(base::Time::FromSecondsSinceUnixEpoch(1700000500));

  std::optional<base::Value> json =
      base::JSONReader::Read(*base::WriteJson(state.ToValue()), base::JSON_PARSE_RFC);
  ASSERT_TRUE(json && json->is_dict());
  FilterListState back = FilterListState::FromValue(json->GetDict());

  ASSERT_EQ(back.lists().size(), 3u);
  const FilterListEntry* e = back.Find(id);
  ASSERT_TRUE(e);
  EXPECT_EQ(e->url, "https://lists.example/a.txt");
  EXPECT_EQ(e->title, "Mine");
  EXPECT_EQ(e->rules, 42);
  EXPECT_EQ(e->updated, base::Time::FromSecondsSinceUnixEpoch(1700000000));
  EXPECT_FALSE(e->builtin);
  EXPECT_EQ(back.Find("easylist")->error, "The list could not be downloaded.");
  EXPECT_EQ(back.exceptions(), std::set<std::string>{"news.test"});
  EXPECT_FALSE(back.auto_update());
  EXPECT_EQ(back.last_update_check(), base::Time::FromSecondsSinceUnixEpoch(1700000500));
}

TEST(FilterListStateTest, HostileFileContentFallsBackToSafeValues) {
  std::string fine_id;
  FilterListState().AddCustomList(GURL("https://fine.example/x"), false, &fine_id);
  std::string text = R"json({
    "auto_update": "yes",
    "exceptions": ["ok.test", "bad host", 5, "../x", "OK2.test"],
    "lists": [
      {"id": "../../etc/passwd", "url": "https://a.example/x"},
      {"id": "cabc", "url": "https://wrong-id.example/x"},
      {"id": "easylist", "url": "https://evil.example/x", "builtin": false},
      {"id": "ID", "url": "http://insecure.example/x"},
      {"id": "easyprivacy", "rules": -7, "error": "<script>"},
      {"id": "FINE_ID", "url": "https://fine.example/x", "rules": -7, "title": 5,
       "error": "<script>"}
    ]
  })json";
  text.replace(text.find("FINE_ID"), 7, fine_id);
  std::optional<base::Value> json = base::JSONReader::Read(text, base::JSON_PARSE_RFC);
  ASSERT_TRUE(json && json->is_dict());
  FilterListState state = FilterListState::FromValue(json->GetDict());
  EXPECT_TRUE(state.auto_update());
  EXPECT_EQ(state.exceptions(), (std::set<std::string>{"ok.test", "ok2.test"}));
  // The two built-in lists, with their own addresses, plus one good custom list.
  ASSERT_EQ(state.lists().size(), 3u);
  EXPECT_EQ(state.Find("easylist")->url, kEasyListUrl);
  EXPECT_TRUE(state.Find("easylist")->builtin);
  EXPECT_EQ(state.Find("easyprivacy")->rules, 0);
  EXPECT_EQ(state.Find("easyprivacy")->error, "");
  const FilterListEntry* custom = state.Find(fine_id);
  ASSERT_TRUE(custom);
  EXPECT_EQ(custom->url, "https://fine.example/x");
  EXPECT_EQ(custom->rules, 0);
  EXPECT_EQ(custom->title, "fine.example");
  EXPECT_EQ(custom->error, "");  // Only known messages come back.
}

TEST(FilterListStateTest, EveryListStartsOn) {
  FilterListState state;
  for (const FilterListEntry& entry : state.lists()) {
    EXPECT_TRUE(entry.enabled) << entry.id;
  }
  EXPECT_FALSE(state.NeedsRebuild());
}

TEST(FilterListStateTest, EasyPrivacyCanBeTurnedOffAndOn) {
  FilterListState state;
  EXPECT_EQ(state.SetEnabled(kEasyPrivacyId, false),
            FilterListState::EnableResult::kOk);
  EXPECT_FALSE(state.Find(kEasyPrivacyId)->enabled);
  EXPECT_TRUE(state.NeedsRebuild());  // The shipped engine has it.
  EXPECT_EQ(state.SetEnabled(kEasyPrivacyId, true),
            FilterListState::EnableResult::kOk);
  EXPECT_TRUE(state.Find(kEasyPrivacyId)->enabled);
  EXPECT_FALSE(state.NeedsRebuild());
}

TEST(FilterListStateTest, EasyListCannotBeTurnedOff) {
  FilterListState state;
  EXPECT_EQ(state.SetEnabled(kEasyListId, false),
            FilterListState::EnableResult::kRequired);
  EXPECT_TRUE(state.Find(kEasyListId)->enabled);
  EXPECT_EQ(state.SetEnabled(kEasyListId, true),
            FilterListState::EnableResult::kOk);
  EXPECT_EQ(state.SetEnabled("nosuchlist", false),
            FilterListState::EnableResult::kUnknownList);
}

TEST(FilterListStateTest, ACustomListCanBeTurnedOffAndStaysListed) {
  FilterListState state;
  std::string id;
  ASSERT_EQ(state.AddCustomList(GURL("https://lists.example/a.txt"), false, &id),
            FilterListState::AddResult::kAdded);
  EXPECT_EQ(state.SetEnabled(id, false), FilterListState::EnableResult::kOk);
  ASSERT_TRUE(state.Find(id));
  EXPECT_FALSE(state.Find(id)->enabled);
  EXPECT_EQ(state.lists().size(), 3u);  // Off is not removed.
}

TEST(FilterListStateTest, EnabledAndIgnoredCountSurviveASaveAndLoad) {
  FilterListState state;
  state.SetEnabled(kEasyPrivacyId, false);
  state.Find(kEasyListId)->ignored_exceptions = 4;
  FilterListState loaded = FilterListState::FromValue(state.ToValue());
  EXPECT_FALSE(loaded.Find(kEasyPrivacyId)->enabled);
  EXPECT_TRUE(loaded.Find(kEasyListId)->enabled);
  EXPECT_EQ(loaded.Find(kEasyListId)->ignored_exceptions, 4);
}

TEST(FilterListStateTest, ADamagedEnabledFieldGivesOn) {
  for (const char* bad : {"\"enabled\": \"no\"", "\"enabled\": 0",
                          "\"enabled\": null", "\"enabled\": []"}) {
    std::string text = std::string(R"json({"lists": [
      {"id": "easyprivacy", "url": "x", "ignored_exceptions": -3, )json") +
                       bad + "}]}";
    std::optional<base::Value> json =
        base::JSONReader::Read(text, base::JSON_PARSE_RFC);
    ASSERT_TRUE(json && json->is_dict()) << bad;
    FilterListState state = FilterListState::FromValue(json->GetDict());
    EXPECT_TRUE(state.Find(kEasyPrivacyId)->enabled) << bad;
    EXPECT_EQ(state.Find(kEasyPrivacyId)->ignored_exceptions, 0) << bad;
  }
}

TEST(FilterListStateTest, AFileThatTurnsEasyListOffIsIgnoredForEasyList) {
  std::optional<base::Value> json = base::JSONReader::Read(
      R"json({"lists": [{"id": "easylist", "url": "x", "enabled": false},
                        {"id": "easyprivacy", "url": "x", "enabled": false}]})json",
      base::JSON_PARSE_RFC);
  ASSERT_TRUE(json && json->is_dict());
  FilterListState state = FilterListState::FromValue(json->GetDict());
  EXPECT_TRUE(state.Find(kEasyListId)->enabled);
  EXPECT_FALSE(state.Find(kEasyPrivacyId)->enabled);
TEST(FilterListStateTest, BlockingLevelDefaultsToAggressive) {
  FilterListState state;
  EXPECT_EQ(state.default_level(), BlockingLevel::kAggressive);
  EXPECT_TRUE(state.level_overrides().empty());
  EXPECT_EQ(FilterListState::FromValue(base::DictValue()).default_level(),
            BlockingLevel::kAggressive);
}

TEST(FilterListStateTest, LevelOverridesAreNormalizedAndCapped) {
  FilterListState state;
  EXPECT_TRUE(state.SetLevelOverride("News.Test", BlockingLevel::kStandard));
  EXPECT_EQ(state.level_overrides().at("news.test"), BlockingLevel::kStandard);
  EXPECT_FALSE(state.SetLevelOverride("not a host", BlockingLevel::kStandard));
  EXPECT_TRUE(state.SetLevelOverride("news.test", BlockingLevel::kAggressive));
  EXPECT_EQ(state.level_overrides().size(), 1u);
  EXPECT_TRUE(state.ClearLevelOverride("news.test"));
  EXPECT_TRUE(state.level_overrides().empty());
  for (size_t i = 0; i < kMaxLevelOverrides; ++i) {
    ASSERT_TRUE(state.SetLevelOverride("h" + std::to_string(i) + ".test",
                                       BlockingLevel::kStandard));
  }
  EXPECT_FALSE(state.SetLevelOverride("one-more.test", BlockingLevel::kStandard));
  EXPECT_TRUE(state.SetLevelOverride("h1.test", BlockingLevel::kAggressive));
  EXPECT_TRUE(state.ClearLevelOverride("h1.test"));  // Removing still works.
}

TEST(FilterListStateTest, FindBlockingLevelPrefersTheNearestHost) {
  std::map<std::string, BlockingLevel> levels = {
      {"news.test", BlockingLevel::kStandard},
      {"ads.news.test", BlockingLevel::kAggressive}};
  EXPECT_EQ(FindBlockingLevel("news.test", levels), BlockingLevel::kStandard);
  EXPECT_EQ(FindBlockingLevel("www.news.test", levels),
            BlockingLevel::kStandard);
  EXPECT_EQ(FindBlockingLevel("x.ads.news.test", levels),
            BlockingLevel::kAggressive);
  EXPECT_EQ(FindBlockingLevel("other.test", levels), std::nullopt);
  EXPECT_EQ(FindBlockingLevel("", levels), std::nullopt);
}

TEST(FilterListStateTest, BlockingLevelsSurviveASaveAndLoad) {
  FilterListState state;
  state.SetDefaultLevel(BlockingLevel::kStandard);
  state.SetLevelOverride("a.test", BlockingLevel::kAggressive);
  state.SetLevelOverride("b.test", BlockingLevel::kStandard);
  std::optional<base::Value> json =
      base::JSONReader::Read(*base::WriteJson(state.ToValue()), base::JSON_PARSE_RFC);
  ASSERT_TRUE(json && json->is_dict());
  FilterListState back = FilterListState::FromValue(json->GetDict());
  EXPECT_EQ(back.default_level(), BlockingLevel::kStandard);
  EXPECT_EQ(back.level_overrides(), state.level_overrides());
}

TEST(FilterListStateTest, HostileLevelContentFallsBackToSafeValues) {
  for (const char* text :
       {R"({"default_level": "loud"})", R"({"default_level": 5})",
        R"({"default_level": null})", R"({"default_level": ["standard"]})"}) {
    std::optional<base::Value> json = base::JSONReader::Read(text, base::JSON_PARSE_RFC);
    ASSERT_TRUE(json && json->is_dict()) << text;
    EXPECT_EQ(FilterListState::FromValue(json->GetDict()).default_level(),
              BlockingLevel::kAggressive)
        << text;
  }
  std::optional<base::Value> json = base::JSONReader::Read(
      R"({"levels": {"ok.test": "standard", "bad host": "standard",
          "x.test": "loud", "y.test": 5, "z.test": null, "OK2.test": "aggressive"}})",
      base::JSON_PARSE_RFC);
  ASSERT_TRUE(json && json->is_dict());
  std::map<std::string, BlockingLevel> expected = {
      {"ok.test", BlockingLevel::kStandard},
      {"ok2.test", BlockingLevel::kAggressive}};
  EXPECT_EQ(FilterListState::FromValue(json->GetDict()).level_overrides(),
            expected);
  for (const char* text : {R"({"levels": ["a.test"]})", R"({"levels": 5})"}) {
    std::optional<base::Value> bad = base::JSONReader::Read(text, base::JSON_PARSE_RFC);
    ASSERT_TRUE(bad && bad->is_dict()) << text;
    EXPECT_TRUE(FilterListState::FromValue(bad->GetDict()).level_overrides().empty());
  }
}

TEST(FilterListStateTest, TooManyLevelOverridesAreCut) {
  base::DictValue dict;
  base::DictValue levels;
  for (size_t i = 0; i < kMaxLevelOverrides + 500; ++i) {
    levels.Set("h" + std::to_string(i) + ".test", "standard");
  }
  dict.Set("levels", std::move(levels));
  EXPECT_EQ(FilterListState::FromValue(dict).level_overrides().size(),
            kMaxLevelOverrides);
}

TEST(FilterListStateTest, GarbageGivesDefaults) {
  FilterListState state = FilterListState::FromValue(base::DictValue());
  EXPECT_EQ(state.lists().size(), 2u);
  EXPECT_TRUE(state.Find(kEasyListId)->enabled);
  EXPECT_TRUE(state.Find(kEasyPrivacyId)->enabled);
  EXPECT_TRUE(state.auto_update());
}

}  // namespace
}  // namespace aerie_adblock
