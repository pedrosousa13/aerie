// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_tab_model/folder_tree.h"

#include <chrono>
#include <ratio>
#include <string>
#include <vector>

#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "base/test/simple_test_clock.h"
#include "components/aerie_tab_model/tab_model.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace aerie_tab_model {

namespace {

class FolderTreeTest : public testing::Test {
 protected:
  FolderTreeTest()
      : model_(&clock_,
               base::BindRepeating(
                   [](int* next) { return "id-" + base::NumberToString((*next)++); },
                   &next_id_)) {
    clock_.SetNow(base::Time::FromMillisecondsSinceUnixEpoch(1'700'000'000'000));
  }

  std::string Folder(const std::string& name,
                     const std::string& parent = std::string(),
                     int position = 0,
                     Section section = Section::kToday,
                     bool expanded = true) {
    FolderRecord folder;
    folder.name = name;
    folder.parent = parent;
    folder.position = position;
    folder.section = section;
    folder.expanded = expanded;
    return model_.AddFolder(std::move(folder));
  }

  std::string Tab(const std::string& parent = std::string(),
                  int position = 0,
                  Section section = Section::kToday) {
    TabRecord tab;
    tab.parent = parent;
    tab.position = position;
    tab.section = section;
    return model_.AddTab(std::move(tab));
  }

  // A chain of `levels` folders, each inside the one before. Returns the ids,
  // outermost first.
  std::vector<std::string> Chain(int levels,
                                 Section section = Section::kToday) {
    std::vector<std::string> ids;
    std::string parent;
    for (int i = 0; i < levels; ++i) {
      parent = Folder("f" + base::NumberToString(i + 1), parent, 0, section);
      ids.push_back(parent);
    }
    return ids;
  }

  static bool All(const std::string&) { return true; }

  std::vector<std::string> Ids(const std::vector<TreeRow>& rows) {
    std::vector<std::string> ids;
    for (const TreeRow& row : rows) {
      ids.push_back(row.id);
    }
    return ids;
  }

  base::SimpleTestClock clock_;
  int next_id_ = 1;
  TabModel model_;
};

}  // namespace

// --- Depth and height ---

TEST_F(FolderTreeTest, DepthCountsFolderLevelsFromTheSection) {
  const std::vector<std::string> chain = Chain(3);
  EXPECT_EQ(Depth(model_, std::string()), 0);
  EXPECT_EQ(Depth(model_, chain[0]), 1);
  EXPECT_EQ(Depth(model_, chain[1]), 2);
  EXPECT_EQ(Depth(model_, chain[2]), 3);
  EXPECT_EQ(Depth(model_, "nope"), 0);
}

TEST_F(FolderTreeTest, SubtreeHeightIsTheDeepestBranchWithTheFolderItself) {
  const std::string top = Folder("top");
  EXPECT_EQ(SubtreeHeight(model_, top), 1);
  const std::string a = Folder("a", top);
  Folder("b", top);
  EXPECT_EQ(SubtreeHeight(model_, top), 2);
  Folder("c", a);
  EXPECT_EQ(SubtreeHeight(model_, top), 3);
  EXPECT_EQ(SubtreeHeight(model_, a), 2);
  EXPECT_EQ(SubtreeHeight(model_, "nope"), 0);
  // Tabs do not count as levels.
  Tab(a);
  EXPECT_EQ(SubtreeHeight(model_, a), 2);
}

// --- CanNest ---

TEST_F(FolderTreeTest, ADepthOfFiveIsAcceptedAndASixthLevelIsRefused) {
  const std::vector<std::string> chain = Chain(4);
  const std::string fresh = Folder("fresh");
  // Into a level-4 folder: the folder becomes level 5.
  EXPECT_TRUE(CanNest(model_, ItemKind::kFolder, fresh, chain[3]));
  EXPECT_TRUE(CanCreateFolderIn(model_, chain[3]));
  // Level 5 now exists.
  const std::string level5 = Folder("f5", chain[3]);
  EXPECT_EQ(Depth(model_, level5), 5);
  // A sixth is refused, for a move and for a new folder.
  const std::string other = Folder("other");
  EXPECT_FALSE(CanNest(model_, ItemKind::kFolder, other, level5));
  EXPECT_FALSE(CanCreateFolderIn(model_, level5));
}

TEST_F(FolderTreeTest, ATabCanGoIntoAFolderOfLevelFive) {
  const std::vector<std::string> chain = Chain(5);
  const std::string tab = Tab();
  EXPECT_TRUE(CanNest(model_, ItemKind::kTab, tab, chain[4]));
}

TEST_F(FolderTreeTest, TheModelItselfRefusesAFolderParentedBelowItself) {
  // The cycle rule lives in the model (PlacementIsValid) and CanNest agrees.
  const std::vector<std::string> chain = Chain(2);
  EXPECT_FALSE(model_.MoveFolder(chain[0], Section::kToday, chain[1], 0));
  EXPECT_FALSE(CanNest(model_, ItemKind::kFolder, chain[0], chain[1]));
}

TEST_F(FolderTreeTest, AFolderIntoItselfOrItsOwnDescendantIsRefused) {
  const std::vector<std::string> chain = Chain(3);
  EXPECT_FALSE(CanNest(model_, ItemKind::kFolder, chain[0], chain[0]));
  EXPECT_FALSE(CanNest(model_, ItemKind::kFolder, chain[0], chain[1]));
  EXPECT_FALSE(CanNest(model_, ItemKind::kFolder, chain[0], chain[2]));
  EXPECT_FALSE(CanNest(model_, ItemKind::kFolder, chain[1], chain[2]));
  // Out of the chain is fine, and so is a move up.
  EXPECT_TRUE(CanNest(model_, ItemKind::kFolder, chain[2], chain[0]));
  EXPECT_TRUE(CanNest(model_, ItemKind::kFolder, chain[2], std::string()));
}

TEST_F(FolderTreeTest, ASubtreeWhoseHeightPushesPastFiveIsRefused) {
  // A target of level 3 and a subtree 3 levels high would reach level 6.
  const std::vector<std::string> target = Chain(3);
  const std::vector<std::string> subtree_chain = Chain(3);
  // Chain() made a second chain starting at a root folder: height 3.
  EXPECT_EQ(SubtreeHeight(model_, subtree_chain[0]), 3);
  EXPECT_FALSE(
      CanNest(model_, ItemKind::kFolder, subtree_chain[0], target[2]));
  // The same subtree fits under a level-2 folder (2 + 3 = 5).
  EXPECT_TRUE(CanNest(model_, ItemKind::kFolder, subtree_chain[0], target[1]));
  // A subtree of height 2 fits under the level-3 folder (3 + 2 = 5).
  EXPECT_TRUE(CanNest(model_, ItemKind::kFolder, subtree_chain[1], target[2]));
}

TEST_F(FolderTreeTest, ASubtreeCountsItsDeepestBranchNotItsFirst) {
  const std::vector<std::string> target = Chain(3);
  const std::string top = Folder("top");
  Folder("shallow", top);
  const std::string deep = Folder("deep", top);
  const std::string deeper = Folder("deeper", deep);
  EXPECT_EQ(SubtreeHeight(model_, top), 3);
  EXPECT_FALSE(CanNest(model_, ItemKind::kFolder, top, target[2]));
  // A folder that already sits in the chain moves with its own height.
  EXPECT_TRUE(CanNest(model_, ItemKind::kFolder, deeper, target[2]));
}

TEST_F(FolderTreeTest, MovingWithinTheSameSubtreeToAShallowerPlaceIsAllowed) {
  const std::vector<std::string> chain = Chain(5);
  // chain[4] is level 5. Moving it to the section root is fine.
  EXPECT_TRUE(CanNest(model_, ItemKind::kFolder, chain[4], std::string()));
}

TEST_F(FolderTreeTest, UnknownItemsAndTargetsAreRefused) {
  const std::string folder = Folder("f");
  EXPECT_FALSE(CanNest(model_, ItemKind::kFolder, "nope", folder));
  EXPECT_FALSE(CanNest(model_, ItemKind::kTab, "nope", folder));
  EXPECT_FALSE(CanNest(model_, ItemKind::kFolder, folder, "nope"));
  EXPECT_FALSE(CanCreateFolderIn(model_, "nope"));
  EXPECT_TRUE(CanCreateFolderIn(model_, std::string()));
}

// --- Visible rows ---

TEST_F(FolderTreeTest, RowsAreADepthFirstWalkInPositionOrder) {
  const std::string trips = Folder("Trips", std::string(), 1);
  const std::string tab_a = Tab(std::string(), 0);
  const std::string tab_b = Tab(trips, 5);
  const std::string sub = Folder("Rome", trips, 2);
  const std::string tab_c = Tab(sub, 0);
  const std::string tab_d = Tab(std::string(), 3);
  Tab(std::string(), 0, Section::kPinned);

  const std::vector<TreeRow> rows =
      BuildVisibleRows(model_, Section::kToday, All);
  EXPECT_EQ(Ids(rows), (std::vector<std::string>{tab_a, trips, sub, tab_c,
                                                  tab_b, tab_d}));
  ASSERT_EQ(rows.size(), 6u);
  EXPECT_EQ(rows[0].level, 0);
  EXPECT_FALSE(rows[0].is_folder);
  EXPECT_EQ(rows[1].level, 0);
  EXPECT_TRUE(rows[1].is_folder);
  EXPECT_EQ(rows[1].item_count, 2);  // sub and tab_b, directly
  EXPECT_EQ(rows[2].level, 1);
  EXPECT_EQ(rows[2].parent, trips);
  EXPECT_EQ(rows[3].level, 2);
  EXPECT_EQ(rows[3].parent, sub);
  EXPECT_EQ(rows[4].level, 1);
  EXPECT_EQ(rows[5].level, 0);
}

TEST_F(FolderTreeTest, ACollapsedFolderHidesItsChildrenButKeepsItsRow) {
  const std::string trips =
      Folder("Trips", std::string(), 0, Section::kToday, /*expanded=*/false);
  Tab(trips, 0);
  Tab(trips, 1);
  const std::vector<TreeRow> rows =
      BuildVisibleRows(model_, Section::kToday, All);
  ASSERT_EQ(rows.size(), 1u);
  EXPECT_EQ(rows[0].id, trips);
  EXPECT_FALSE(rows[0].expanded);
  EXPECT_EQ(rows[0].item_count, 2);
  // BuildAllRows sees through the fold.
  EXPECT_EQ(BuildAllRows(model_, Section::kToday, All).size(), 3u);
}

TEST_F(FolderTreeTest, AFolderComesBeforeATabAtTheSamePositionThenTheIdDecides) {
  const std::string tab = Tab(std::string(), 0);
  const std::string folder = Folder("F", std::string(), 0);
  EXPECT_EQ(Ids(BuildVisibleRows(model_, Section::kToday, All)),
            (std::vector<std::string>{folder, tab}));
}

TEST_F(FolderTreeTest, ArchivedTabsAndUnlistedTabsHaveNoRow) {
  const std::string folder = Folder("F");
  const std::string live = Tab(folder, 0);
  const std::string archived = Tab(folder, 1);
  const std::string unlisted = Tab(folder, 2);
  model_.UpdateTab(archived, [&](TabRecord& t) { t.archived_at = clock_.Now(); });
  const std::vector<TreeRow> rows = BuildVisibleRows(
      model_, Section::kToday,
      [&](const std::string& id) { return id != unlisted; });
  EXPECT_EQ(Ids(rows), (std::vector<std::string>{folder, live}));
  // The count follows what is listed.
  EXPECT_EQ(rows[0].item_count, 1);
}

TEST_F(FolderTreeTest, EachSectionHasItsOwnTree) {
  const std::string pinned_folder =
      Folder("P", std::string(), 0, Section::kPinned);
  const std::string today_folder = Folder("T");
  EXPECT_EQ(Ids(BuildVisibleRows(model_, Section::kPinned, All)),
            (std::vector<std::string>{pinned_folder}));
  EXPECT_EQ(Ids(BuildVisibleRows(model_, Section::kToday, All)),
            (std::vector<std::string>{today_folder}));
}

TEST_F(FolderTreeTest, FolderContainsTabLooksThroughNestedFolders) {
  const std::vector<std::string> chain = Chain(3);
  const std::string deep = Tab(chain[2]);
  const std::string loose = Tab();
  EXPECT_TRUE(FolderContainsTab(model_, chain[0], deep));
  EXPECT_TRUE(FolderContainsTab(model_, chain[2], deep));
  EXPECT_FALSE(FolderContainsTab(model_, chain[0], loose));
  EXPECT_FALSE(FolderContainsTab(model_, "nope", deep));
}

TEST_F(FolderTreeTest, FolderChoicesListTheFoldersOfASectionInTreeOrder) {
  const std::string a = Folder("A", std::string(), 0);
  const std::string b = Folder("B", a, 0);
  const std::string c = Folder("C", std::string(), 1);
  Folder("P", std::string(), 0, Section::kPinned);
  const std::vector<FolderChoice> choices =
      FolderChoices(model_, Section::kToday);
  ASSERT_EQ(choices.size(), 3u);
  EXPECT_EQ(choices[0].id, a);
  EXPECT_EQ(choices[0].level, 1);
  EXPECT_EQ(choices[1].id, b);
  EXPECT_EQ(choices[1].level, 2);
  EXPECT_EQ(choices[2].id, c);
  EXPECT_EQ(choices[2].name, "C");
}

// --- Edits ---

TEST_F(FolderTreeTest, CreateFolderMakesAnExpandedManualFolderAtTheTop) {
  Tab(std::string(), 4);
  const std::string existing = Folder("old", std::string(), 2);
  const std::string made =
      CreateFolder(model_, Section::kToday, std::string(), "New folder");
  ASSERT_FALSE(made.empty());
  const FolderRecord* record = model_.GetFolder(made);
  EXPECT_EQ(record->name, "New folder");
  EXPECT_TRUE(record->expanded);
  EXPECT_EQ(record->kind, FolderKind::kManual);
  EXPECT_LT(record->position, 2);
  const std::vector<TreeRow> rows =
      BuildVisibleRows(model_, Section::kToday, All);
  EXPECT_EQ(rows.front().id, made);
  EXPECT_EQ(rows[1].id, existing);
}

TEST_F(FolderTreeTest, CreateFolderInsideAFolderGoesFirstThere) {
  const std::string parent = Folder("p");
  const std::string tab = Tab(parent, 3);
  const std::string made = CreateFolder(model_, Section::kToday, parent, "in");
  EXPECT_EQ(model_.GetFolder(made)->parent, parent);
  EXPECT_EQ(Ids(BuildVisibleRows(model_, Section::kToday, All)),
            (std::vector<std::string>{parent, made, tab}));
}

TEST_F(FolderTreeTest, CreateFolderIsRefusedDeeperThanFive) {
  const std::vector<std::string> chain = Chain(5);
  EXPECT_TRUE(
      CreateFolder(model_, Section::kToday, chain[4], "six").empty());
  EXPECT_FALSE(
      CreateFolder(model_, Section::kToday, chain[3], "five").empty());
  EXPECT_TRUE(CreateFolder(model_, Section::kToday, "nope", "x").empty());
}

TEST_F(FolderTreeTest, SetFolderExpandedWritesOnceAndStamps) {
  const std::string folder = Folder("f", std::string(), 0, Section::kToday,
                                    /*expanded=*/false);
  const int64_t before = model_.next_change();
  EXPECT_TRUE(SetFolderExpanded(model_, folder, true));
  EXPECT_TRUE(model_.GetFolder(folder)->expanded);
  EXPECT_GT(model_.next_change(), before);
  const int64_t after = model_.next_change();
  // The same value writes nothing.
  EXPECT_TRUE(SetFolderExpanded(model_, folder, true));
  EXPECT_EQ(model_.next_change(), after);
  EXPECT_FALSE(SetFolderExpanded(model_, "nope", true));
}

TEST_F(FolderTreeTest, ANameIsTrimmedCutAndAnEmptyOneKeepsTheOldName) {
  EXPECT_EQ(NormalizeFolderName("  Trips  ", "Old"), "Trips");
  EXPECT_EQ(NormalizeFolderName("", "Old"), std::nullopt);
  EXPECT_EQ(NormalizeFolderName("   ", "Old"), std::nullopt);
  EXPECT_EQ(NormalizeFolderName("Old", "Old"), std::nullopt);
  const std::string long_name(300, 'x');
  EXPECT_EQ(NormalizeFolderName(long_name, "Old")->size(), 200u);
}

TEST_F(FolderTreeTest, RenameFolderWritesTheNewNameOnly) {
  const std::string folder = Folder("Old");
  EXPECT_TRUE(RenameFolder(model_, folder, " New "));
  EXPECT_EQ(model_.GetFolder(folder)->name, "New");
  EXPECT_FALSE(RenameFolder(model_, folder, ""));
  EXPECT_FALSE(RenameFolder(model_, folder, "New"));
  EXPECT_EQ(model_.GetFolder(folder)->name, "New");
  EXPECT_FALSE(RenameFolder(model_, "nope", "x"));
}

TEST_F(FolderTreeTest, FolderIsEmptyLooksAtTabsAndFolders) {
  const std::string folder = Folder("f");
  EXPECT_TRUE(FolderIsEmpty(model_, folder));
  const std::string tab = Tab(folder);
  EXPECT_FALSE(FolderIsEmpty(model_, folder));
  model_.RemoveTab(tab);
  EXPECT_TRUE(FolderIsEmpty(model_, folder));
  Folder("child", folder);
  EXPECT_FALSE(FolderIsEmpty(model_, folder));
}

TEST_F(FolderTreeTest, DeleteMovesTheContentsOutInOrderIntoTheFoldersPlace) {
  const std::string before = Tab(std::string(), 0);
  const std::string folder = Folder("F", std::string(), 1);
  const std::string after = Tab(std::string(), 2);
  const std::string inner_b = Tab(folder, 7);
  const std::string inner_a = Tab(folder, 3);
  const std::string inner_folder = Folder("Inner", folder, 5);
  ASSERT_TRUE(DeleteFolder(model_, folder));
  EXPECT_EQ(model_.GetFolder(folder), nullptr);
  const std::vector<TreeRow> rows =
      BuildVisibleRows(model_, Section::kToday, All);
  EXPECT_EQ(Ids(rows), (std::vector<std::string>{before, inner_a, inner_folder,
                                                  inner_b, after}));
  for (const TreeRow& row : rows) {
    EXPECT_EQ(row.level, 0) << row.id;
  }
}

TEST_F(FolderTreeTest, DeleteOfANestedFolderMovesTheContentsToItsParent) {
  const std::vector<std::string> chain = Chain(2);
  const std::string tab = Tab(chain[1]);
  ASSERT_TRUE(DeleteFolder(model_, chain[1]));
  EXPECT_EQ(model_.GetTab(tab)->parent, chain[0]);
  EXPECT_FALSE(DeleteFolder(model_, chain[1]));
}

TEST_F(FolderTreeTest, MoveTabToFolderPutsTheTabLastAndRespectsTheLimit) {
  const std::string folder = Folder("F");
  const std::string first = Tab(folder, 4);
  const std::string tab = Tab(std::string(), 0);
  ASSERT_TRUE(MoveTabToFolder(model_, tab, folder));
  EXPECT_EQ(model_.GetTab(tab)->parent, folder);
  EXPECT_EQ(Ids(BuildVisibleRows(model_, Section::kToday, All)),
            (std::vector<std::string>{folder, first, tab}));
  EXPECT_FALSE(MoveTabToFolder(model_, tab, "nope"));
  EXPECT_FALSE(MoveTabToFolder(model_, "nope", folder));
}

TEST_F(FolderTreeTest, MoveTabOutPutsItNextToTheFolderItLeaves) {
  const std::string folder = Folder("F", std::string(), 3);
  const std::string inside = Tab(folder, 0);
  const std::string sibling = Tab(std::string(), 4);
  ASSERT_TRUE(MoveTabToFolder(model_, inside, std::string()));
  EXPECT_EQ(model_.GetTab(inside)->parent, std::string());
  EXPECT_EQ(Ids(BuildVisibleRows(model_, Section::kToday, All)),
            (std::vector<std::string>{folder, inside, sibling}));
}

TEST_F(FolderTreeTest, MoveToNewFolderMakesTheFolderWhereTheTabWas) {
  const std::string first = Tab(std::string(), 0);
  const std::string tab = Tab(std::string(), 1);
  const std::string last = Tab(std::string(), 2);
  const std::string folder = MoveTabToNewFolder(model_, tab, "New folder");
  ASSERT_FALSE(folder.empty());
  EXPECT_EQ(model_.GetTab(tab)->parent, folder);
  EXPECT_EQ(model_.GetFolder(folder)->section, Section::kToday);
  EXPECT_TRUE(model_.GetFolder(folder)->expanded);
  EXPECT_EQ(Ids(BuildVisibleRows(model_, Section::kToday, All)),
            (std::vector<std::string>{first, folder, tab, last}));
  EXPECT_TRUE(MoveTabToNewFolder(model_, "nope", "x").empty());
}

TEST_F(FolderTreeTest, MoveToNewFolderInsideALevelFiveFolderIsRefused) {
  const std::vector<std::string> chain = Chain(5);
  const std::string tab = Tab(chain[4]);
  EXPECT_TRUE(MoveTabToNewFolder(model_, tab, "six").empty());
  EXPECT_EQ(model_.GetTab(tab)->parent, chain[4]);
}

TEST_F(FolderTreeTest, MoveToNewFolderWorksInPinned) {
  const std::string tab = Tab(std::string(), 0, Section::kPinned);
  const std::string folder = MoveTabToNewFolder(model_, tab, "P");
  ASSERT_FALSE(folder.empty());
  EXPECT_EQ(model_.GetFolder(folder)->section, Section::kPinned);
}

TEST_F(FolderTreeTest, FavoritesHaveNoFolders) {
  const std::string tab = Tab(std::string(), 0, Section::kFavorite);
  EXPECT_TRUE(MoveTabToNewFolder(model_, tab, "x").empty());
  EXPECT_TRUE(
      CreateFolder(model_, Section::kFavorite, std::string(), "x").empty());
}

// --- Speed (issue #181: 300 tabs in 20 folders build in under 16 ms) ---

TEST_F(FolderTreeTest, ThreeHundredTabsInTwentyFoldersBuildInUnderSixteenMs) {
  for (int f = 0; f < 20; ++f) {
    const std::string folder =
        Folder("F" + base::NumberToString(f), std::string(), f);
    // Two of them nest one level.
    const std::string inner =
        f % 10 == 0 ? Folder("inner", folder, 100) : std::string();
    for (int t = 0; t < 15; ++t) {
      Tab(inner.empty() || t % 2 ? folder : inner, t);
    }
  }
  ASSERT_EQ(model_.tabs().size(), 300u);
  ASSERT_GE(model_.folders().size(), 20u);
  const auto start = std::chrono::steady_clock::now();
  std::vector<TreeRow> rows;
  for (int i = 0; i < 10; ++i) {
    rows = BuildVisibleRows(model_, Section::kToday, All);
  }
  const double per_build_ms =
      std::chrono::duration<double, std::milli>(
          std::chrono::steady_clock::now() - start)
          .count() /
      10;
  EXPECT_EQ(rows.size(), 300u + model_.folders().size());
  EXPECT_LT(per_build_ms, 16.0);
  // The number goes into the pull request.
  RecordProperty("tree_build_ms", base::NumberToString(per_build_ms));
  LOG(INFO) << "tree build, 300 tabs in 20 folders: " << per_build_ms << " ms";
}

TEST_F(FolderTreeTest, AFolderWithOnlyRecordsAwaitingARestoredTabIsEmpty) {
  const std::string folder = Folder("f");
  const std::string tab = Tab(folder);
  ASSERT_TRUE(model_.MarkAwaitingRestore(tab));
  EXPECT_TRUE(FolderIsEmpty(model_, folder));
}

}  // namespace aerie_tab_model
