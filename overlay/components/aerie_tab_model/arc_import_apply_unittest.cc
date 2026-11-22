// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_tab_model/arc_import_apply.h"

#include <string>
#include <utility>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/json/json_writer.h"
#include "base/strings/string_number_conversions.h"
#include "base/test/simple_test_clock.h"
#include "base/time/time.h"
#include "components/aerie_tab_model/folder_tree.h"
#include "components/aerie_tab_model/pinned_tabs.h"
#include "components/aerie_tab_model/tab_model.h"
#include "components/aerie_tab_model/tab_model_persistence.h"
#include "components/aerie_tab_model/tab_model_types.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace aerie_tab_model {

namespace {

ArcPlanNode Tab(const std::string& title,
                std::optional<std::string> custom = std::nullopt) {
  ArcPlanNode node;
  node.tab.url = "https://" + title + ".example/";
  node.tab.last_title = title;
  node.tab.custom_name = std::move(custom);
  return node;
}

ArcPlanNode Folder(const std::string& name, std::vector<ArcPlanNode> children) {
  ArcPlanNode node;
  node.is_folder = true;
  node.folder_name = name;
  node.children = std::move(children);
  return node;
}

class ArcImportApplyTest : public testing::Test {
 protected:
  ArcImportApplyTest()
      : model_(&clock_,
               base::BindRepeating(
                   [](int* next) { return "id-" + base::NumberToString((*next)++); },
                   &next_id_)) {
    clock_.SetNow(base::Time::FromMillisecondsSinceUnixEpoch(1'700'000'000'000));
  }

  std::string AddExistingTab(Section section, int position) {
    TabRecord tab;
    tab.section = section;
    tab.position = position;
    tab.pinned_url = "https://existing.example/" + base::NumberToString(position);
    return model_.AddTab(std::move(tab));
  }

  // "t:<title>" for a tab and "f:<name>" for a folder, in tree order.
  std::vector<std::string> Children(const std::string& folder) {
    std::vector<std::string> out;
    for (const ChildRef& child : OrderedChildren(
             model_, Section::kPinned, folder, /*listed_only=*/false,
             [](const std::string&) { return true; })) {
      if (child.is_folder) {
        out.push_back("f:" + model_.GetFolder(child.id)->name);
      } else {
        out.push_back("t:" + model_.GetTab(child.id)->last_title.value_or("?"));
      }
    }
    return out;
  }

  std::string FolderId(const std::string& name) {
    for (const auto& [id, folder] : model_.folders()) {
      if (folder.name == name) {
        return id;
      }
    }
    return std::string();
  }

  base::SimpleTestClock clock_;
  int next_id_ = 1;
  TabModel model_;
};

}  // namespace

TEST_F(ArcImportApplyTest, PinnedKeepsNestingAndOrder) {
  ArcSpacePlan plan;
  plan.pinned.push_back(Tab("a"));
  plan.pinned.push_back(Folder("Work", {Tab("b"), Folder("Deep", {Tab("c")}),
                                        Tab("d")}));
  plan.pinned.push_back(Tab("e"));

  std::optional<ArcImportCounts> counts = ApplyArcSpacePlan(model_, plan);
  ASSERT_TRUE(counts.has_value());
  EXPECT_EQ(counts->pinned_tabs, 5);
  EXPECT_EQ(counts->folders, 2);
  EXPECT_EQ(Children(""), (std::vector<std::string>{"t:a", "f:Work", "t:e"}));
  EXPECT_EQ(Children(FolderId("Work")),
            (std::vector<std::string>{"t:b", "f:Deep", "t:d"}));
  EXPECT_EQ(Children(FolderId("Deep")), (std::vector<std::string>{"t:c"}));
  EXPECT_EQ(Depth(model_, FolderId("Deep")), 2);
}

TEST_F(ArcImportApplyTest, ImportedItemsComeAfterWhatTheUserHas) {
  const std::string first = AddExistingTab(Section::kPinned, 0);
  const std::string second = AddExistingTab(Section::kPinned, 1);
  const TabRecord before_first = *model_.GetTab(first);

  ArcSpacePlan plan;
  plan.pinned.push_back(Tab("a"));
  plan.pinned.push_back(Tab("b"));
  ASSERT_TRUE(ApplyArcSpacePlan(model_, plan).has_value());

  const std::vector<ChildRef> children =
      OrderedChildren(model_, Section::kPinned, "", false,
                      [](const std::string&) { return true; });
  ASSERT_EQ(children.size(), 4u);
  EXPECT_EQ(children[0].id, first);
  EXPECT_EQ(children[1].id, second);
  EXPECT_EQ(model_.GetTab(children[2].id)->last_title, "a");
  EXPECT_EQ(model_.GetTab(children[3].id)->last_title, "b");
  // What was there is the same record.
  EXPECT_EQ(model_.GetTab(first)->position, before_first.position);
  EXPECT_EQ(model_.GetTab(first)->pinned_url, before_first.pinned_url);
  EXPECT_EQ(model_.GetTab(first)->change, before_first.change);
}

TEST_F(ArcImportApplyTest, FavoritesStopAtTheCapCountingWhatExists) {
  for (int i = 0; i < 10; ++i) {
    ASSERT_FALSE(AddExistingTab(Section::kFavorite, i).empty());
  }
  ArcSpacePlan plan;
  for (int i = 0; i < 5; ++i) {
    plan.favorites.push_back(Tab("fav" + base::NumberToString(i)).tab);
  }
  std::optional<ArcImportCounts> counts = ApplyArcSpacePlan(model_, plan);
  ASSERT_TRUE(counts.has_value());
  EXPECT_EQ(counts->favorites, 2);
  EXPECT_EQ(counts->skipped[ArcSkipReason::kFavoritesFull], 3);
  EXPECT_EQ(SectionTabIds(model_, Section::kFavorite).size(), kFavoritesCap);
}

TEST_F(ArcImportApplyTest, TabsAreUnloadedRecordsWithTheirNames) {
  ArcSpacePlan plan;
  plan.pinned.push_back(Tab("Inbox", "My Mail"));
  plan.pinned.push_back(Tab("plain"));
  std::optional<ArcImportCounts> counts = ApplyArcSpacePlan(model_, plan);
  ASSERT_TRUE(counts.has_value());
  EXPECT_EQ(counts->custom_names, 1);
  bool found_named = false;
  for (const auto& [id, tab] : model_.tabs()) {
    EXPECT_EQ(tab.section, Section::kPinned);
    EXPECT_TRUE(tab.sleeping);
    EXPECT_TRUE(tab.pinned_url.has_value());
    if (tab.last_title == "Inbox") {
      found_named = true;
      EXPECT_EQ(tab.manual_name, "My Mail");
      EXPECT_EQ(tab.pinned_url, "https://Inbox.example/");
    } else {
      EXPECT_FALSE(tab.manual_name.has_value());
    }
  }
  EXPECT_TRUE(found_named);
}

TEST_F(ArcImportApplyTest, AFolderWithoutANameGetsTheGivenOne) {
  ArcSpacePlan plan;
  plan.pinned.push_back(Folder("", {Tab("a")}));
  ArcApplyOptions options;
  options.untitled_folder_name = "Untitled folder";
  ASSERT_TRUE(ApplyArcSpacePlan(model_, plan, options).has_value());
  EXPECT_EQ(Children(""), (std::vector<std::string>{"f:Untitled folder"}));
}

TEST_F(ArcImportApplyTest, ASkipOfThePlanIsCarriedToTheResult) {
  ArcSpacePlan plan;
  plan.pinned.push_back(Tab("a"));
  plan.skipped[ArcSkipReason::kUnsupportedScheme] = 4;
  std::optional<ArcImportCounts> counts = ApplyArcSpacePlan(model_, plan);
  ASSERT_TRUE(counts.has_value());
  EXPECT_EQ(counts->skipped[ArcSkipReason::kUnsupportedScheme], 4);
}

TEST_F(ArcImportApplyTest, AnImportOver250TabsBringsInTheFirst250AndNamesTheRest) {
  ArcSpacePlan plan;
  for (int i = 0; i < 300; ++i) {
    plan.pinned.push_back(Tab("t" + base::NumberToString(i)));
  }
  std::optional<ArcImportCounts> counts = ApplyArcSpacePlan(model_, plan);
  ASSERT_TRUE(counts.has_value());
  EXPECT_EQ(counts->pinned_tabs, 250);
  EXPECT_EQ(counts->skipped[ArcSkipReason::kOverCap], 50);
  const std::vector<std::string> children = Children("");
  ASSERT_EQ(children.size(), 250u);
  EXPECT_EQ(children.front(), "t:t0");
  EXPECT_EQ(children.back(), "t:t249");
}

TEST_F(ArcImportApplyTest, ThePinnedCapCountsTheTabsTheWorkspaceHasAlready) {
  for (int i = 0; i < 900; ++i) {
    AddExistingTab(Section::kPinned, i);
  }
  ArcSpacePlan plan;
  for (int i = 0; i < 150; ++i) {
    plan.pinned.push_back(Tab("t" + base::NumberToString(i)));
  }
  std::optional<ArcImportCounts> counts = ApplyArcSpacePlan(model_, plan);
  ASSERT_TRUE(counts.has_value());
  EXPECT_EQ(counts->pinned_tabs, 100);
  EXPECT_EQ(counts->skipped[ArcSkipReason::kOverCap], 50);
}

TEST_F(ArcImportApplyTest, TheCapCutsInsideAFolderAndLaterItemsAreSkippedToo) {
  ArcSpacePlan plan;
  std::vector<ArcPlanNode> inside;
  for (int i = 0; i < 248; ++i) {
    inside.push_back(Tab("a" + base::NumberToString(i)));
  }
  plan.pinned.push_back(Folder("Work", std::move(inside)));
  for (int i = 0; i < 5; ++i) {
    plan.pinned.push_back(Tab("b" + base::NumberToString(i)));
  }
  plan.favorites.push_back(Tab("fav").tab);
  std::optional<ArcImportCounts> counts = ApplyArcSpacePlan(model_, plan);
  ASSERT_TRUE(counts.has_value());
  // 248 in the folder, b0 and b1 fit; b2..b4 and the Favorite do not.
  EXPECT_EQ(counts->pinned_tabs, 250);
  EXPECT_EQ(counts->favorites, 0);
  EXPECT_EQ(counts->skipped[ArcSkipReason::kOverCap], 4);
}

// The M2 security sweep's plan (#275): 2200 tabs, 8000-byte addresses and
// 1024-character titles, about 17 MB of Arc file. It wrote a 20 MB state that
// the reader refused.
TEST_F(ArcImportApplyTest, TheSweepPlanAppliesAndTheStateReadsBackOk) {
  ArcSpacePlan plan;
  for (int i = 0; i < 2200; ++i) {
    ArcPlanNode node;
    const std::string number = base::NumberToString(i);
    node.tab.url = "https://example.com/" + number + "/" +
                   std::string(8000 - 21 - number.size(), 'a');
    node.tab.last_title = std::string(1024, 'T');
    plan.pinned.push_back(std::move(node));
  }
  std::optional<ArcImportCounts> counts = ApplyArcSpacePlan(model_, plan);
  ASSERT_TRUE(counts.has_value());
  EXPECT_GT(counts->pinned_tabs, 0);
  EXPECT_EQ(counts->pinned_tabs + counts->skipped[ArcSkipReason::kOverCap],
            2200);

  std::optional<std::string> json = base::WriteJson(model_.ToValue());
  ASSERT_TRUE(json.has_value());
  // Well under the limit of the reader: room for the rest of the Tab Model.
  EXPECT_LT(json->size(), kMaxStateBytes / 4);

  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  const base::FilePath path = dir.GetPath().AppendASCII(kStateFile);
  ASSERT_TRUE(base::WriteFile(path, *json));
  EXPECT_EQ(ReadStateFile(path).status, LoadStatus::kOk);
}

TEST_F(ArcImportApplyTest, ANoFolderIsMadeForATabThatIsOverTheCap) {
  ArcSpacePlan plan;
  for (int i = 0; i < 250; ++i) {
    plan.pinned.push_back(Tab("t" + base::NumberToString(i)));
  }
  plan.pinned.push_back(Folder("X", {Tab("inside")}));
  std::optional<ArcImportCounts> counts = ApplyArcSpacePlan(model_, plan);
  ASSERT_TRUE(counts.has_value());
  EXPECT_EQ(counts->pinned_tabs, 250);
  EXPECT_EQ(counts->folders, 0);
  EXPECT_EQ(FolderId("X"), "");
  EXPECT_EQ(counts->skipped[ArcSkipReason::kOverCap], 1);
}

TEST_F(ArcImportApplyTest, ThePinnedCapEndsOnlyPinnedAndFavoritesStillCome) {
  for (int i = 0; i < 999; ++i) {
    AddExistingTab(Section::kPinned, i);
  }
  ArcSpacePlan plan;
  for (int i = 0; i < 5; ++i) {
    plan.pinned.push_back(Tab("t" + base::NumberToString(i)));
  }
  for (int i = 0; i < 3; ++i) {
    plan.favorites.push_back(Tab("f" + base::NumberToString(i)).tab);
  }
  std::optional<ArcImportCounts> counts = ApplyArcSpacePlan(model_, plan);
  ASSERT_TRUE(counts.has_value());
  EXPECT_EQ(counts->pinned_tabs, 1);
  EXPECT_EQ(counts->skipped[ArcSkipReason::kOverCap], 4);
  EXPECT_EQ(counts->favorites, 3);
}

TEST_F(ArcImportApplyTest, AFailedWriteChangesNothing) {
  const std::string existing = AddExistingTab(Section::kPinned, 0);
  const std::string favorite = AddExistingTab(Section::kFavorite, 0);
  const size_t tabs_before = model_.tabs().size();
  const size_t folders_before = model_.folders().size();

  ArcSpacePlan plan;
  plan.pinned.push_back(Tab("a"));
  plan.pinned.push_back(Folder("Work", {Tab("b"), Tab("c")}));
  plan.favorites.push_back(Tab("fav").tab);

  // Every record is the one that fails, in turn: a, Work, b, c and fav.
  for (int fail = 0; fail < 5; ++fail) {
    ArcApplyOptions options;
    options.fail_at_record_for_testing = fail;
    std::optional<ArcImportCounts> counts =
        ApplyArcSpacePlan(model_, plan, options);
    EXPECT_FALSE(counts.has_value()) << fail;
    EXPECT_EQ(model_.tabs().size(), tabs_before) << fail;
    EXPECT_EQ(model_.folders().size(), folders_before) << fail;
    ASSERT_TRUE(model_.GetTab(existing)) << fail;
    ASSERT_TRUE(model_.GetTab(favorite)) << fail;
  }
  // Without a failure the same plan imports.
  EXPECT_TRUE(ApplyArcSpacePlan(model_, plan).has_value());
  EXPECT_EQ(model_.tabs().size(), tabs_before + 4);
}

}  // namespace aerie_tab_model
