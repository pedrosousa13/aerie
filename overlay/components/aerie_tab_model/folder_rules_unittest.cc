// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_tab_model/folder_rules.h"

#include <memory>
#include <string>

#include "base/functional/bind.h"
#include "base/strings/string_number_conversions.h"
#include "base/test/simple_test_clock.h"
#include "components/aerie_tab_model/folder_tree.h"
#include "components/aerie_tab_model/tab_model.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace aerie_tab_model {

namespace {

class FolderRulesTest : public testing::Test {
 protected:
  FolderRulesTest()
      : model_(&clock_,
               base::BindRepeating(
                   [](int* next) { return "id-" + base::NumberToString((*next)++); },
                   &next_id_)) {
    clock_.SetNow(base::Time::FromMillisecondsSinceUnixEpoch(1'700'000'000'000));
    default_tab_ = NewTab();
  }

  // An Auto-filing folder with one domain rule.
  std::string AutoFolder(Section section,
                         const std::string& domain,
                         const std::string& parent = std::string()) {
    std::string id = CreateFolder(model_, section, parent, "F " + domain);
    EXPECT_TRUE(SetFolderRuleKind(model_, id, FolderKind::kAuto));
    EXPECT_FALSE(AddRule(model_, id, RuleType::kDomain, domain).empty());
    return id;
  }

  std::string NewTab() {
    TabRecord tab;
    tab.last_active = clock_.Now();
    return model_.AddTab(std::move(tab));
  }

  std::optional<AutoFileTarget> Find(const std::string& url,
                                     const std::string& title = std::string(),
                                     const std::string& tab = std::string(),
                                     bool enabled = true) {
    return FindAutoFileTarget(model_, GURL(url), title,
                              tab.empty() ? default_tab_ : tab, enabled);
  }

  base::SimpleTestClock clock_;
  int next_id_ = 1;
  TabModel model_;
  // A tab that exists, for the lookups that are not about one tab.
  std::string default_tab_;
};

TEST_F(FolderRulesTest, RulesSurviveAWriteAndARead) {
  std::string folder = AutoFolder(Section::kToday, "example.com");
  std::string second =
      AddRule(model_, folder, RuleType::kTitleContains, "Trip");
  ASSERT_FALSE(second.empty());
  ASSERT_TRUE(SetRuleEnabled(model_, folder, second, false));
  ASSERT_TRUE(AddExcludedHost(model_, folder, "News.Example.com"));
  TabRecord tab;
  tab.last_active = clock_.Now();
  const std::string tab_id = model_.AddTab(std::move(tab));
  ASSERT_TRUE(AddExcludedTab(model_, folder, tab_id));
  ASSERT_TRUE(SetFolderRulesEnabled(model_, folder, false));

  // The whole model through state.json and back.
  ParseResult parsed = ParseState(model_.ToValue());
  ASSERT_EQ(LoadStatus::kOk, parsed.status);
  const FolderRecord* stored = nullptr;
  for (const FolderRecord& f : parsed.state->folders) {
    if (f.id == folder) {
      stored = &f;
    }
  }
  ASSERT_TRUE(stored);
  EXPECT_EQ(FolderKind::kAuto, stored->kind);
  FolderRules rules = ReadFolderRules(*stored);
  EXPECT_FALSE(rules.enabled);
  ASSERT_EQ(2u, rules.rules.size());
  EXPECT_EQ(RuleType::kDomain, rules.rules[0].type);
  EXPECT_EQ("example.com", rules.rules[0].value);
  EXPECT_TRUE(rules.rules[0].enabled);
  EXPECT_EQ(RuleType::kTitleContains, rules.rules[1].type);
  EXPECT_FALSE(rules.rules[1].enabled);
  EXPECT_EQ(std::vector<std::string>{"news.example.com"}, rules.excluded_hosts);
  EXPECT_EQ(std::vector<std::string>{tab_id}, rules.excluded_tabs);
}

TEST_F(FolderRulesTest, AFolderWithNoRulesReadsAsOnWithNone) {
  std::string folder = CreateFolder(model_, Section::kToday, "", "Plain");
  FolderRules rules = ReadFolderRules(*model_.GetFolder(folder));
  EXPECT_TRUE(rules.enabled);
  EXPECT_TRUE(rules.rules.empty());
}

TEST_F(FolderRulesTest, ADamagedRulesDictionaryDropsOnlyTheBadParts) {
  std::string folder = CreateFolder(model_, Section::kToday, "", "Odd");
  base::DictValue dict;
  base::ListValue list;
  base::DictValue good;
  good.Set("id", "g");
  good.Set("type", "domain");
  good.Set("value", "example.com");
  list.Append(std::move(good));
  base::DictValue regex;
  regex.Set("id", "x");
  regex.Set("type", "regex");
  regex.Set("value", ".*");
  list.Append(std::move(regex));
  list.Append("junk");
  dict.Set("rules", std::move(list));
  dict.Set("excluded_hosts", "not a list");
  model_.UpdateFolder(folder, [&](FolderRecord& f) { f.rules = std::move(dict); });
  FolderRules rules = ReadFolderRules(*model_.GetFolder(folder));
  ASSERT_EQ(1u, rules.rules.size());
  EXPECT_EQ("g", rules.rules[0].id);
  EXPECT_TRUE(rules.rules[0].enabled);
  EXPECT_TRUE(rules.excluded_hosts.empty());
}

TEST_F(FolderRulesTest, AddRuleRefusesBlankValuesAndUnknownFolders) {
  std::string folder = CreateFolder(model_, Section::kToday, "", "F");
  EXPECT_TRUE(AddRule(model_, folder, RuleType::kUrlContains, "   ").empty());
  EXPECT_TRUE(AddRule(model_, folder, RuleType::kDomain, "://").empty());
  EXPECT_TRUE(AddRule(model_, "nope", RuleType::kUrlContains, "x").empty());
  EXPECT_TRUE(ReadFolderRules(*model_.GetFolder(folder)).rules.empty());
}

TEST_F(FolderRulesTest, AddRuleHasAnUpperBound) {
  std::string folder = CreateFolder(model_, Section::kToday, "", "F");
  for (size_t i = 0; i < kMaxRulesPerFolder; ++i) {
    ASSERT_FALSE(
        AddRule(model_, folder, RuleType::kUrlContains, base::NumberToString(i))
            .empty());
  }
  EXPECT_TRUE(AddRule(model_, folder, RuleType::kUrlContains, "one more").empty());
}

TEST_F(FolderRulesTest, UpdateAndRemoveRule) {
  std::string folder = CreateFolder(model_, Section::kToday, "", "F");
  std::string rule = AddRule(model_, folder, RuleType::kUrlContains, "a");
  EXPECT_TRUE(UpdateRule(model_, folder, rule, RuleType::kTitleContains, "b"));
  FolderRules rules = ReadFolderRules(*model_.GetFolder(folder));
  EXPECT_EQ(RuleType::kTitleContains, rules.rules[0].type);
  EXPECT_EQ("b", rules.rules[0].value);
  EXPECT_FALSE(UpdateRule(model_, folder, rule, RuleType::kTitleContains, " "));
  EXPECT_FALSE(UpdateRule(model_, folder, "nope", RuleType::kDomain, "x.com"));
  EXPECT_TRUE(RemoveRule(model_, folder, rule));
  EXPECT_FALSE(RemoveRule(model_, folder, rule));
  EXPECT_TRUE(ReadFolderRules(*model_.GetFolder(folder)).rules.empty());
}

TEST_F(FolderRulesTest, EditsApplyAtOnce) {
  // Each edit is one write of the folder record: the model counter moves.
  std::string folder = CreateFolder(model_, Section::kToday, "", "F");
  const int64_t before = model_.next_change();
  AddRule(model_, folder, RuleType::kUrlContains, "a");
  EXPECT_EQ(before + 1, model_.next_change());
}

TEST_F(FolderRulesTest, ExcludedHostIsNormalisedAndNotDuplicated) {
  std::string folder = CreateFolder(model_, Section::kToday, "", "F");
  EXPECT_TRUE(AddExcludedHost(model_, folder, "Example.com."));
  EXPECT_FALSE(AddExcludedHost(model_, folder, "example.com"));
  EXPECT_FALSE(AddExcludedHost(model_, folder, ""));
  EXPECT_TRUE(RemoveExcludedHost(model_, folder, "EXAMPLE.com"));
  EXPECT_FALSE(RemoveExcludedHost(model_, folder, "example.com"));
}

TEST_F(FolderRulesTest, KindSwitchKeepsRulesButManualDoesNotFile) {
  std::string folder = AutoFolder(Section::kToday, "example.com");
  ASSERT_TRUE(Find("https://example.com/"));
  ASSERT_TRUE(SetFolderRuleKind(model_, folder, FolderKind::kManual));
  EXPECT_FALSE(Find("https://example.com/"));
  EXPECT_EQ(1u, ReadFolderRules(*model_.GetFolder(folder)).rules.size());
  ASSERT_TRUE(SetFolderRuleKind(model_, folder, FolderKind::kAuto));
  EXPECT_TRUE(Find("https://example.com/"));
}

TEST_F(FolderRulesTest, SmartIsNotThisSlices) {
  std::string folder = CreateFolder(model_, Section::kToday, "", "F");
  EXPECT_FALSE(SetFolderRuleKind(model_, folder, FolderKind::kSmart));
  EXPECT_EQ(FolderKind::kManual, model_.GetFolder(folder)->kind);
  EXPECT_FALSE(SetFolderRuleKind(model_, "nope", FolderKind::kAuto));
}

TEST_F(FolderRulesTest, NoMatchWhenNoFolderMatches) {
  AutoFolder(Section::kToday, "example.com");
  EXPECT_FALSE(Find("https://elsewhere.test/"));
  EXPECT_FALSE(Find("https://badexample.com/"));
}

TEST_F(FolderRulesTest, TheFirstFolderTopToBottomWins) {
  std::string first = AutoFolder(Section::kToday, "example.com");
  // CreateFolder puts a folder at the top, so this one is above `first`.
  std::string top = AutoFolder(Section::kToday, "example.com");
  auto target = Find("https://example.com/");
  ASSERT_TRUE(target);
  EXPECT_EQ(top, target->folder_id);
  // Move the other one above it: the order of the tree decides, not creation.
  model_.MoveFolder(first, Section::kToday, "", FirstPosition(model_, Section::kToday, ""));
  target = Find("https://example.com/");
  ASSERT_TRUE(target);
  EXPECT_EQ(first, target->folder_id);
}

TEST_F(FolderRulesTest, APinnedFolderBeatsATodayFolder) {
  std::string today = AutoFolder(Section::kToday, "example.com");
  std::string pinned = AutoFolder(Section::kPinned, "example.com");
  auto target = Find("https://example.com/");
  ASSERT_TRUE(target);
  EXPECT_EQ(pinned, target->folder_id);
  EXPECT_NE(today, target->folder_id);
}

TEST_F(FolderRulesTest, ANestedFolderIsAskedInDepthFirstOrder) {
  std::string outer = AutoFolder(Section::kToday, "nothing.test");
  std::string inner = AutoFolder(Section::kToday, "example.com", outer);
  std::string later = AutoFolder(Section::kToday, "example.com");
  // `later` was made last, so it is the top folder: it wins over the nested one.
  auto target = Find("https://example.com/");
  ASSERT_TRUE(target);
  EXPECT_EQ(later, target->folder_id);
  model_.MoveFolder(later, Section::kToday, "", 100);
  target = Find("https://example.com/");
  ASSERT_TRUE(target);
  EXPECT_EQ(inner, target->folder_id);
}

TEST_F(FolderRulesTest, AManualFolderIsNeverAsked) {
  std::string manual = CreateFolder(model_, Section::kToday, "", "M");
  AddRule(model_, manual, RuleType::kDomain, "example.com");
  EXPECT_FALSE(Find("https://example.com/"));
}

TEST_F(FolderRulesTest, EachSwitchStopsFiling) {
  std::string folder = AutoFolder(Section::kToday, "example.com");
  std::string rule = ReadFolderRules(*model_.GetFolder(folder)).rules[0].id;
  ASSERT_TRUE(Find("https://example.com/"));
  // The rule's switch.
  ASSERT_TRUE(SetRuleEnabled(model_, folder, rule, false));
  EXPECT_FALSE(Find("https://example.com/"));
  ASSERT_TRUE(SetRuleEnabled(model_, folder, rule, true));
  EXPECT_TRUE(Find("https://example.com/"));
  // The folder's "All" switch.
  ASSERT_TRUE(SetFolderRulesEnabled(model_, folder, false));
  EXPECT_FALSE(Find("https://example.com/"));
  ASSERT_TRUE(SetFolderRulesEnabled(model_, folder, true));
  EXPECT_TRUE(Find("https://example.com/"));
  // "Folder rules everywhere".
  EXPECT_FALSE(Find("https://example.com/", "", "", /*enabled=*/false));
}

TEST_F(FolderRulesTest, AFolderThatDoesNotMatchLetsTheNextOneTry) {
  std::string today = AutoFolder(Section::kToday, "example.com");
  std::string off = AutoFolder(Section::kToday, "example.com");
  SetFolderRulesEnabled(model_, off, false);
  auto target = Find("https://example.com/");
  ASSERT_TRUE(target);
  EXPECT_EQ(today, target->folder_id);
}

TEST_F(FolderRulesTest, ExceptionsStopOneFolderOnly) {
  std::string top = AutoFolder(Section::kToday, "example.com");
  std::string below = AutoFolder(Section::kToday, "example.com");
  // `below` is the top folder (made last); exclude the host there.
  AddExcludedHost(model_, below, "example.com");
  auto target = Find("https://example.com/");
  ASSERT_TRUE(target);
  EXPECT_EQ(top, target->folder_id);
}

TEST_F(FolderRulesTest, FilingMovesTheTabToTheTopOfTheFolderAndRecordsWhy) {
  std::string folder = AutoFolder(Section::kToday, "example.com");
  std::string older = NewTab();
  ASSERT_TRUE(model_.MoveTab(older, Section::kToday, folder, 0));
  std::string tab = NewTab();
  auto target = Find("https://example.com/", "", tab);
  ASSERT_TRUE(target);
  ASSERT_TRUE(FileTabIntoFolder(model_, tab, *target, "https://example.com/"));
  const TabRecord* record = model_.GetTab(tab);
  EXPECT_EQ(folder, record->parent);
  EXPECT_EQ(Section::kToday, record->section);
  EXPECT_LT(record->position, model_.GetTab(older)->position);
  ASSERT_TRUE(record->filed_by);
  EXPECT_EQ(folder, record->filed_by->folder_id);
  EXPECT_EQ(target->rule_id, record->filed_by->rule_id);
  EXPECT_FALSE(record->pinned_url);
}

TEST_F(FolderRulesTest, FilingIntoAPinnedFolderPinsTheTab) {
  std::string folder = AutoFolder(Section::kPinned, "example.com");
  std::string tab = NewTab();
  auto target = Find("https://example.com/a", "", tab);
  ASSERT_TRUE(target);
  ASSERT_TRUE(FileTabIntoFolder(model_, tab, *target, "https://example.com/a"));
  const TabRecord* record = model_.GetTab(tab);
  EXPECT_EQ(Section::kPinned, record->section);
  EXPECT_EQ(folder, record->parent);
  EXPECT_EQ("https://example.com/a", record->pinned_url);
}

TEST_F(FolderRulesTest, UndoPutsTheTabBackAtTheTopOfToday) {
  AutoFolder(Section::kPinned, "example.com");
  std::string other = NewTab();
  std::string tab = NewTab();
  auto target = Find("https://example.com/", "", tab);
  ASSERT_TRUE(FileTabIntoFolder(model_, tab, *target, "https://example.com/"));
  ASSERT_TRUE(UndoFiling(model_, tab));
  const TabRecord* record = model_.GetTab(tab);
  EXPECT_EQ(Section::kToday, record->section);
  EXPECT_TRUE(record->parent.empty());
  EXPECT_FALSE(record->pinned_url);
  EXPECT_FALSE(record->filed_by);
  EXPECT_LT(record->position, model_.GetTab(other)->position);
  // A second Undo has nothing to undo.
  EXPECT_FALSE(UndoFiling(model_, tab));
}

TEST_F(FolderRulesTest, LeavingTheFolderBySomeOtherWayClearsFiledBy) {
  std::string folder = AutoFolder(Section::kToday, "example.com");
  std::string tab = NewTab();
  auto target = Find("https://example.com/", "", tab);
  ASSERT_TRUE(FileTabIntoFolder(model_, tab, *target, ""));
  ASSERT_TRUE(model_.GetTab(tab)->filed_by);
  ASSERT_TRUE(MoveTabToFolder(model_, tab, ""));
  EXPECT_FALSE(model_.GetTab(tab)->filed_by);
  EXPECT_FALSE(UndoFiling(model_, tab));

  // Deleting the folder moves a filed tab out, so it is not filed any more.
  std::string second = NewTab();
  target = Find("https://example.com/", "", second);
  ASSERT_TRUE(FileTabIntoFolder(model_, second, *target, ""));
  ASSERT_TRUE(DeleteFolder(model_, folder));
  EXPECT_FALSE(model_.GetTab(second)->filed_by);
}

TEST_F(FolderRulesTest, StopRuleMatchingThisTab) {
  std::string folder = AutoFolder(Section::kToday, "example.com");
  std::string tab = NewTab();
  auto target = Find("https://example.com/", "", tab);
  ASSERT_TRUE(FileTabIntoFolder(model_, tab, *target, ""));
  ASSERT_TRUE(StopRuleMatchingTab(model_, tab));
  EXPECT_EQ(std::vector<std::string>{tab},
            ReadFolderRules(*model_.GetFolder(folder)).excluded_tabs);
  EXPECT_FALSE(Find("https://example.com/", "", tab));
  // Other tabs are still filed.
  EXPECT_TRUE(Find("https://example.com/", "", NewTab()));
  // A tab that is not filed has nothing to stop.
  EXPECT_FALSE(StopRuleMatchingTab(model_, NewTab()));
}

TEST_F(FolderRulesTest, FiledByIsSavedAndRead) {
  std::string folder = AutoFolder(Section::kPinned, "example.com");
  std::string tab = NewTab();
  auto target = Find("https://example.com/", "", tab);
  ASSERT_TRUE(FileTabIntoFolder(model_, tab, *target, "https://example.com/"));
  ParseResult parsed = ParseState(model_.ToValue());
  ASSERT_EQ(LoadStatus::kOk, parsed.status);
  const TabRecord* stored = nullptr;
  for (const TabRecord& t : parsed.state->tabs) {
    if (t.id == tab) {
      stored = &t;
    }
  }
  ASSERT_TRUE(stored);
  ASSERT_TRUE(stored->filed_by);
  EXPECT_EQ(folder, stored->filed_by->folder_id);
  EXPECT_EQ(target->rule_id, stored->filed_by->rule_id);
  // A file written before filing existed has no `filed_by`.
  base::DictValue old = TabToValue(*stored);
  old.Remove("filed_by");
  std::optional<TabRecord> read = TabFromValue(old);
  ASSERT_TRUE(read);
  EXPECT_FALSE(read->filed_by);
  // A damaged one is refused as a whole, like every other field.
  old.Set("filed_by", "x");
  EXPECT_FALSE(TabFromValue(old));
}

TEST_F(FolderRulesTest, FilingRefusesAFavoriteAndAnUnknownFolder) {
  std::string folder = AutoFolder(Section::kToday, "example.com");
  std::string tab = NewTab();
  AutoFileTarget target{folder, "r"};
  EXPECT_FALSE(FileTabIntoFolder(model_, tab, AutoFileTarget{"nope", "r"}, ""));
  EXPECT_FALSE(FileTabIntoFolder(model_, "nope", target, ""));
  ASSERT_TRUE(model_.MoveTab(tab, Section::kFavorite, "", 0));
  EXPECT_FALSE(FileTabIntoFolder(model_, tab, target, ""));
}

}  // namespace

}  // namespace aerie_tab_model
