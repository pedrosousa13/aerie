// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_tab_model/smart_folder.h"

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "base/functional/bind.h"
#include "base/strings/string_number_conversions.h"
#include "base/test/simple_test_clock.h"
#include "base/time/time.h"
#include "components/aerie_tab_model/folder_rules.h"
#include "components/aerie_tab_model/folder_tree.h"
#include "components/aerie_tab_model/tab_model.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace aerie_tab_model {

namespace {

class SmartFolderTest : public testing::Test {
 protected:
  SmartFolderTest()
      : model_(&clock_,
               base::BindRepeating(
                   [](int* next) { return "id-" + base::NumberToString((*next)++); },
                   &next_id_)) {
    clock_.SetNow(base::Time::FromMillisecondsSinceUnixEpoch(1'700'000'000'000));
  }

  // A smart folder with one domain rule.
  std::string SmartFolder(Section section,
                          const std::string& domain,
                          const std::string& parent = std::string()) {
    std::string id = CreateFolder(model_, section, parent, "S " + domain);
    EXPECT_TRUE(SetFolderRuleKind(model_, id, FolderKind::kSmart));
    if (!domain.empty()) {
      EXPECT_FALSE(AddRule(model_, id, RuleType::kDomain, domain).empty());
    }
    return id;
  }

  std::string Tab(const std::string& url,
                  Section section = Section::kToday,
                  const std::string& title = std::string(),
                  const std::string& parent = std::string()) {
    TabRecord tab;
    tab.section = section;
    tab.parent = parent;
    tab.last_active = clock_.Now();
    std::string id = model_.AddTab(std::move(tab));
    facts_[id] = TabFacts{GURL(url), title};
    return id;
  }

  std::optional<TabFacts> FactsFor(const std::string& id) const {
    auto it = facts_.find(id);
    if (it == facts_.end()) {
      return std::nullopt;
    }
    return it->second;
  }

  void Refresh(const std::string& folder) {
    index_.RefreshFolder(
        model_, folder,
        [this](const std::string& id) { return FactsFor(id); });
  }

  // The tab changed address or title.
  bool Navigate(const std::string& id,
                const std::string& url,
                const std::string& title = std::string()) {
    facts_[id] = TabFacts{GURL(url), title};
    return index_.UpdateTab(model_, id, FactsFor(id));
  }

  bool IsMember(const std::string& folder, const std::string& tab) const {
    return index_.Members(folder).contains(tab);
  }

  std::vector<TreeRow> Rows(Section section, bool rules_enabled = true) {
    auto listed = [this](const std::string& id) {
      return facts_.contains(id);
    };
    return ExpandSmartFolders(model_, index_, rules_enabled, section,
                              BuildVisibleRows(model_, section, listed),
                              listed);
  }

  base::SimpleTestClock clock_;
  int next_id_ = 1;
  TabModel model_;
  std::map<std::string, TabFacts> facts_;
  SmartFolderIndex index_;
};

TEST_F(SmartFolderTest, TheRowIdNamesTheFolderAndTheRealTab) {
  const std::string id = MakeSmartRowId("folder-1", "tab-9");
  std::optional<SmartRowRef> ref = ParseSmartRowId(id);
  ASSERT_TRUE(ref);
  EXPECT_EQ("folder-1", ref->folder_id);
  EXPECT_EQ("tab-9", ref->tab_id);
  EXPECT_FALSE(ParseSmartRowId("tab-9"));
  EXPECT_FALSE(ParseSmartRowId("smart:only-one-part"));
}

TEST_F(SmartFolderTest, ListsTheMatchingPinnedAndTodayTabsOfTheWorkspace) {
  const std::string folder = SmartFolder(Section::kToday, "docs.test");
  const std::string today = Tab("https://docs.test/a");
  const std::string pinned = Tab("https://docs.test/b", Section::kPinned);
  const std::string other = Tab("https://elsewhere.test/");
  Refresh(folder);
  EXPECT_TRUE(IsMember(folder, today));
  EXPECT_TRUE(IsMember(folder, pinned));
  EXPECT_FALSE(IsMember(folder, other));
}

TEST_F(SmartFolderTest, LeavesOutFavoritesArchivedTabsAndOtherWorkspaces) {
  const std::string folder = SmartFolder(Section::kToday, "docs.test");
  const std::string favorite = Tab("https://docs.test/f", Section::kFavorite);
  const std::string archived = Tab("https://docs.test/x");
  model_.UpdateTab(archived, [&](TabRecord& tab) {
    tab.archived_at = clock_.Now();
  });
  TabRecord foreign;
  foreign.workspace_id = "other";
  const std::string elsewhere = model_.AddTab(std::move(foreign));
  ASSERT_FALSE(elsewhere.empty());
  facts_[elsewhere] = TabFacts{GURL("https://docs.test/w"), ""};
  Refresh(folder);
  EXPECT_TRUE(index_.Members(folder).empty());
}

TEST_F(SmartFolderTest, SleepingTabsAreIncluded) {
  const std::string folder = SmartFolder(Section::kToday, "docs.test");
  const std::string sleeper = Tab("https://docs.test/z");
  model_.UpdateTab(sleeper, [](TabRecord& tab) { tab.sleeping = true; });
  Refresh(folder);
  EXPECT_TRUE(IsMember(folder, sleeper));
}

TEST_F(SmartFolderTest, ATabInsideAManualFolderStillMatches) {
  const std::string manual = CreateFolder(model_, Section::kToday, "", "Work");
  const std::string folder = SmartFolder(Section::kToday, "docs.test");
  const std::string tab =
      Tab("https://docs.test/a", Section::kToday, "", manual);
  Refresh(folder);
  EXPECT_TRUE(IsMember(folder, tab));
}

TEST_F(SmartFolderTest, ANavigationAddsAndRemovesTheTab) {
  const std::string folder = SmartFolder(Section::kToday, "docs.test");
  const std::string tab = Tab("https://elsewhere.test/");
  Refresh(folder);
  ASSERT_FALSE(IsMember(folder, tab));
  EXPECT_TRUE(Navigate(tab, "https://docs.test/page"));
  EXPECT_TRUE(IsMember(folder, tab));
  EXPECT_FALSE(Navigate(tab, "https://docs.test/other"));  // no change
  EXPECT_TRUE(Navigate(tab, "https://elsewhere.test/again"));
  EXPECT_FALSE(IsMember(folder, tab));
}

TEST_F(SmartFolderTest, ATitleChangeCanMatchATitleRule) {
  const std::string folder = SmartFolder(Section::kToday, "");
  ASSERT_FALSE(
      AddRule(model_, folder, RuleType::kTitleContains, "Roadmap").empty());
  const std::string tab = Tab("https://a.test/", Section::kToday, "Loading");
  Refresh(folder);
  ASSERT_FALSE(IsMember(folder, tab));
  EXPECT_TRUE(Navigate(tab, "https://a.test/", "Q4 roadmap"));
  EXPECT_TRUE(IsMember(folder, tab));
}

TEST_F(SmartFolderTest, ATabThatChangesSectionEntersOrLeaves) {
  const std::string folder = SmartFolder(Section::kToday, "docs.test");
  const std::string tab = Tab("https://docs.test/a");
  Refresh(folder);
  ASSERT_TRUE(IsMember(folder, tab));
  ASSERT_TRUE(model_.MoveTab(tab, Section::kFavorite, "", 0));
  EXPECT_TRUE(index_.UpdateTab(model_, tab, FactsFor(tab)));
  EXPECT_FALSE(IsMember(folder, tab));
  ASSERT_TRUE(model_.MoveTab(tab, Section::kPinned, "", 0));
  EXPECT_TRUE(index_.UpdateTab(model_, tab, FactsFor(tab)));
  EXPECT_TRUE(IsMember(folder, tab));
}

TEST_F(SmartFolderTest, AClosedTabLeavesEveryFolder) {
  const std::string a = SmartFolder(Section::kToday, "docs.test");
  const std::string b = SmartFolder(Section::kPinned, "docs.test");
  const std::string tab = Tab("https://docs.test/a");
  Refresh(a);
  Refresh(b);
  ASSERT_TRUE(IsMember(a, tab) && IsMember(b, tab));
  EXPECT_TRUE(index_.RemoveTab(tab));
  EXPECT_FALSE(IsMember(a, tab));
  EXPECT_FALSE(IsMember(b, tab));
  EXPECT_FALSE(index_.RemoveTab(tab));
}

TEST_F(SmartFolderTest, ATabWithNoFactsIsNotListed) {
  const std::string folder = SmartFolder(Section::kToday, "docs.test");
  TabRecord record;
  const std::string tab = model_.AddTab(std::move(record));
  Refresh(folder);
  EXPECT_FALSE(IsMember(folder, tab));
}

TEST_F(SmartFolderTest, ARuleChangeIsSeenAfterARefresh) {
  const std::string folder = SmartFolder(Section::kToday, "docs.test");
  const std::string tab = Tab("https://news.test/a");
  Refresh(folder);
  ASSERT_FALSE(IsMember(folder, tab));
  ASSERT_FALSE(AddRule(model_, folder, RuleType::kDomain, "news.test").empty());
  Refresh(folder);
  EXPECT_TRUE(IsMember(folder, tab));
}

TEST_F(SmartFolderTest, TheRuleSwitchTheAllSwitchAndExceptionsEmptyTheFolder) {
  const std::string folder = SmartFolder(Section::kToday, "docs.test");
  const std::string rule = ReadFolderRules(*model_.GetFolder(folder)).rules[0].id;
  const std::string tab = Tab("https://docs.test/a");
  Refresh(folder);
  ASSERT_TRUE(IsMember(folder, tab));

  ASSERT_TRUE(SetRuleEnabled(model_, folder, rule, false));
  Refresh(folder);
  EXPECT_TRUE(index_.Members(folder).empty());
  ASSERT_TRUE(SetRuleEnabled(model_, folder, rule, true));

  ASSERT_TRUE(SetFolderRulesEnabled(model_, folder, false));
  Refresh(folder);
  EXPECT_TRUE(index_.Members(folder).empty());
  ASSERT_TRUE(SetFolderRulesEnabled(model_, folder, true));

  ASSERT_TRUE(AddExcludedTab(model_, folder, tab));
  Refresh(folder);
  EXPECT_TRUE(index_.Members(folder).empty());
}

TEST_F(SmartFolderTest, OnlyASmartFolderHasMembers) {
  const std::string folder = SmartFolder(Section::kToday, "docs.test");
  const std::string tab = Tab("https://docs.test/a");
  Refresh(folder);
  ASSERT_TRUE(IsMember(folder, tab));
  ASSERT_TRUE(SetFolderRuleKind(model_, folder, FolderKind::kManual));
  Refresh(folder);
  EXPECT_TRUE(index_.Members(folder).empty());
  EXPECT_FALSE(index_.HasSmartFolders());
  ASSERT_TRUE(SetFolderRuleKind(model_, folder, FolderKind::kAuto));
  Refresh(folder);
  EXPECT_TRUE(index_.Members(folder).empty());
}

TEST_F(SmartFolderTest, AutoFilingNeverFilesIntoASmartFolder) {
  SmartFolder(Section::kToday, "docs.test");
  EXPECT_FALSE(FindAutoFileTarget(model_, GURL("https://docs.test/a"), "",
                                  Tab("https://docs.test/a"), true));
}

TEST_F(SmartFolderTest, ARemovedFolderIsForgotten) {
  const std::string folder = SmartFolder(Section::kToday, "docs.test");
  Tab("https://docs.test/a");
  Refresh(folder);
  ASSERT_TRUE(index_.HasSmartFolders());
  index_.RemoveFolder(folder);
  EXPECT_FALSE(index_.HasSmartFolders());
  EXPECT_TRUE(index_.Members(folder).empty());
}

TEST_F(SmartFolderTest, RowsFollowTheFolderAtTheNextLevelWithTheRealTabsName) {
  const std::string folder = SmartFolder(Section::kToday, "docs.test");
  const std::string today = Tab("https://docs.test/a");
  const std::string pinned = Tab("https://docs.test/b", Section::kPinned);
  Refresh(folder);

  const std::vector<TreeRow> rows = Rows(Section::kToday);
  // The folder, then the Pinned match before the Today match, then the real
  // Today tab with its own row. The real Pinned tab is in the Pinned list.
  ASSERT_EQ(4u, rows.size());
  EXPECT_TRUE(rows[0].is_folder);
  EXPECT_EQ(folder, rows[0].id);
  EXPECT_EQ(2, rows[0].item_count);

  EXPECT_FALSE(rows[1].is_folder);
  EXPECT_TRUE(rows[1].smart_member);
  EXPECT_EQ(MakeSmartRowId(folder, pinned), rows[1].id);
  EXPECT_EQ(pinned, rows[1].source_id);
  EXPECT_EQ(Section::kPinned, rows[1].origin);
  EXPECT_EQ(folder, rows[1].parent);
  EXPECT_EQ(1, rows[1].level);

  EXPECT_TRUE(rows[2].smart_member);
  EXPECT_EQ(today, rows[2].source_id);
  EXPECT_EQ(Section::kToday, rows[2].origin);
}

TEST_F(SmartFolderTest, ARealRowAndItsSmartRowBothExist) {
  const std::string folder = SmartFolder(Section::kToday, "docs.test");
  const std::string tab = Tab("https://docs.test/a");
  Refresh(folder);
  const std::vector<TreeRow> rows = Rows(Section::kToday);
  int real = 0;
  int smart = 0;
  for (const TreeRow& row : rows) {
    real += row.id == tab;
    smart += row.source_id == tab;
  }
  EXPECT_EQ(1, real);
  EXPECT_EQ(1, smart);
}

TEST_F(SmartFolderTest, AShutFolderShowsItsCountAndNoRows) {
  const std::string folder = SmartFolder(Section::kToday, "docs.test");
  Tab("https://docs.test/a");
  Tab("https://docs.test/b");
  Refresh(folder);
  ASSERT_TRUE(SetFolderExpanded(model_, folder, false));
  const std::vector<TreeRow> rows = Rows(Section::kToday);
  ASSERT_EQ(3u, rows.size());  // the folder and the two real tabs
  EXPECT_EQ(folder, rows[0].id);
  EXPECT_EQ(2, rows[0].item_count);
  EXPECT_FALSE(rows[1].smart_member);
  EXPECT_FALSE(rows[2].smart_member);
}

TEST_F(SmartFolderTest, TheGlobalSwitchEmptiesEverySmartFolder) {
  const std::string folder = SmartFolder(Section::kToday, "docs.test");
  Tab("https://docs.test/a");
  Refresh(folder);
  const std::vector<TreeRow> rows = Rows(Section::kToday, /*rules_enabled=*/false);
  ASSERT_EQ(2u, rows.size());  // the folder and the one real tab
  EXPECT_EQ(0, rows[0].item_count);
  EXPECT_FALSE(rows[1].smart_member);
}

TEST_F(SmartFolderTest, ASmartFolderInPinnedListsTodayTabsToo) {
  const std::string folder = SmartFolder(Section::kPinned, "docs.test");
  const std::string today = Tab("https://docs.test/a");
  Refresh(folder);
  const std::vector<TreeRow> rows = Rows(Section::kPinned);
  ASSERT_EQ(2u, rows.size());
  EXPECT_EQ(today, rows[1].source_id);
  EXPECT_EQ(Section::kToday, rows[1].origin);
}

TEST_F(SmartFolderTest, ASmartFolderInsideAFolderIndentsItsRows) {
  const std::string outer = CreateFolder(model_, Section::kToday, "", "Outer");
  const std::string folder = SmartFolder(Section::kToday, "docs.test", outer);
  Tab("https://docs.test/a");
  Refresh(folder);
  const std::vector<TreeRow> rows = Rows(Section::kToday);
  // Outer (level 0), the smart folder (1), its member (2), the real tab (0).
  ASSERT_EQ(4u, rows.size());
  EXPECT_EQ(1, rows[1].level);
  EXPECT_TRUE(rows[2].smart_member);
  EXPECT_EQ(2, rows[2].level);
}

TEST_F(SmartFolderTest, TwoSmartFoldersMayListTheSameTab) {
  const std::string a = SmartFolder(Section::kToday, "docs.test");
  const std::string b = SmartFolder(Section::kToday, "docs.test");
  const std::string tab = Tab("https://docs.test/a");
  Refresh(a);
  Refresh(b);
  EXPECT_NE(MakeSmartRowId(a, tab), MakeSmartRowId(b, tab));
  int smart = 0;
  for (const TreeRow& row : Rows(Section::kToday)) {
    smart += row.smart_member;
  }
  EXPECT_EQ(2, smart);
}

// One tab changes in a window of 2,000 tabs and 20 smart folders. The matching
// work is one evaluation per smart folder, not one per tab, and it takes under
// a millisecond.
TEST_F(SmartFolderTest, ATitleChangeCostsOneEvaluationPerFolderNotPerTab) {
  std::vector<std::string> folders;
  for (int i = 0; i < 20; ++i) {
    const std::string folder = SmartFolder(
        Section::kToday, "site" + base::NumberToString(i) + ".test");
    ASSERT_FALSE(
        AddRule(model_, folder, RuleType::kTitleContains,
                "keyword" + base::NumberToString(i))
            .empty());
    folders.push_back(folder);
  }
  std::vector<std::string> tabs;
  for (int i = 0; i < 2000; ++i) {
    tabs.push_back(Tab("https://site" + base::NumberToString(i % 40) +
                           ".test/page" + base::NumberToString(i),
                       i % 7 == 0 ? Section::kPinned : Section::kToday,
                       "Title " + base::NumberToString(i)));
  }
  for (const std::string& folder : folders) {
    Refresh(folder);
  }

  const std::string& target = tabs[1234];
  base::TimeDelta best = base::TimeDelta::Max();
  for (int run = 0; run < 25; ++run) {
    facts_[target] = TabFacts{GURL("https://site3.test/x"),
                              "Title keyword" + base::NumberToString(run)};
    const base::TimeTicks start = base::TimeTicks::Now();
    index_.UpdateTab(model_, target, FactsFor(target));
    best = std::min(best, base::TimeTicks::Now() - start);
    EXPECT_EQ(20u, index_.last_evaluations());
  }
  EXPECT_LT(best, base::Milliseconds(1));
}

}  // namespace

TEST_F(SmartFolderTest, ARowIdWithColonsInTheFolderIdStaysUnambiguous) {
  // Loaded record ids are only checked for being non-empty.
  const std::string folder = "f:1\\x";
  const std::string tab = "t:2";
  std::optional<SmartRowRef> ref = ParseSmartRowId(MakeSmartRowId(folder, tab));
  ASSERT_TRUE(ref);
  EXPECT_EQ(folder, ref->folder_id);
  EXPECT_EQ(tab, ref->tab_id);
  // Two different pairs never give one id.
  EXPECT_NE(MakeSmartRowId("a:b", "c"), MakeSmartRowId("a", "b:c"));
  EXPECT_EQ("a:b", ParseSmartRowId(MakeSmartRowId("a:b", "c"))->folder_id);
  EXPECT_EQ("b:c", ParseSmartRowId(MakeSmartRowId("a", "b:c"))->tab_id);
  // A dangling escape is not a row id.
  EXPECT_FALSE(ParseSmartRowId("smart:abc\\"));
}

TEST_F(SmartFolderTest, ARefreshThatChangedNothingTheFolderListsByMatchesNoTab) {
  const std::string folder = SmartFolder(Section::kToday, "docs.test");
  for (int i = 0; i < 50; ++i) {
    Tab("https://other" + base::NumberToString(i) + ".test/");
  }
  Tab("https://docs.test/");
  Refresh(folder);
  EXPECT_EQ(51u, index_.last_scanned());
  // It opened, was renamed and moved: nothing it lists by changed.
  model_.UpdateFolder(folder, [](FolderRecord& f) {
    f.expanded = !f.expanded;
    f.name = "Renamed";
  });
  Refresh(folder);
  EXPECT_EQ(0u, index_.last_scanned());
  EXPECT_EQ(1u, index_.Members(folder).size());
  // A rule change scans again.
  ASSERT_FALSE(AddRule(model_, folder, RuleType::kDomain, "other1.test").empty());
  Refresh(folder);
  EXPECT_EQ(51u, index_.last_scanned());
  EXPECT_EQ(2u, index_.Members(folder).size());
  // So does a switch.
  ASSERT_TRUE(SetFolderRulesEnabled(model_, folder, false));
  Refresh(folder);
  EXPECT_EQ(51u, index_.last_scanned());
}

}  // namespace aerie_tab_model
