// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_sidebar/aerie_sidebar_drop_target.h"

#include "base/functional/bind.h"
#include "base/strings/string_number_conversions.h"
#include "base/test/simple_test_clock.h"
#include "components/aerie_tab_model/tab_model.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace aerie_sidebar {

namespace {

using aerie_tab_model::FolderRecord;
using aerie_tab_model::ItemKind;
using aerie_tab_model::Section;
using aerie_tab_model::TabRecord;

constexpr int kRowHeight = 100;
constexpr int kTop = 1000;

class DropTargetTest : public testing::Test {
 protected:
  DropTargetTest()
      : model_(&clock_,
               base::BindRepeating(
                   [](int* next) { return "id-" + base::NumberToString((*next)++); },
                   &next_id_)) {
    clock_.SetNow(base::Time::FromMillisecondsSinceUnixEpoch(1'700'000'000'000));
  }

  std::string Folder(const std::string& parent, int position,
                     Section section = Section::kToday, bool expanded = true) {
    FolderRecord folder;
    folder.parent = parent;
    folder.position = position;
    folder.section = section;
    folder.expanded = expanded;
    return model_.AddFolder(std::move(folder));
  }
  std::string Tab(const std::string& parent, int position,
                  Section section = Section::kToday) {
    TabRecord tab;
    tab.parent = parent;
    tab.position = position;
    tab.section = section;
    return model_.AddTab(std::move(tab));
  }

  // Lists of both sections, 100 px per row. Today starts at kTop; Pinned sits
  // above it, 0..kTop.
  DropGeometry Geometry() {
    DropGeometry g;
    auto fill = [&](Section section, DropList& list, int top) {
      list.section = section;
      list.top = top;
      int y = top;
      for (const aerie_tab_model::TreeRow& row : aerie_tab_model::BuildVisibleRows(
               model_, section, [](const std::string&) { return true; })) {
        list.rows.push_back({row, y, kRowHeight});
        y += kRowHeight;
      }
      list.bottom = std::max(y, top + kRowHeight);
    };
    fill(Section::kPinned, g.pinned, 0);
    g.pinned.bottom = kTop - 1;
    fill(Section::kToday, g.today, kTop);
    return g;
  }

  DropTarget At(const DragItem& item, int y_in_today_row_index,
                int percent, int x = 10) {
    return ComputeDropTarget(model_, x,
                             kTop + y_in_today_row_index * kRowHeight + percent,
                             Geometry(), item);
  }

  base::SimpleTestClock clock_;
  int next_id_ = 1;
  aerie_tab_model::TabModel model_;
};

DragItem TabItem(const std::string& id) { return {ItemKind::kTab, id}; }
DragItem FolderItem(const std::string& id) { return {ItemKind::kFolder, id}; }

}  // namespace

TEST_F(DropTargetTest, TheTopAndBottomFifthOfAFolderRowMeanBeforeAndAfter) {
  const std::string dragged = Tab("", 0);
  const std::string folder = Folder("", 1, Section::kToday, /*expanded=*/false);
  Tab(folder, 0);
  // Rows: dragged, folder.
  DropTarget before = At(TabItem(dragged), 1, 10);
  EXPECT_EQ(before.kind, DropKind::kBefore);
  EXPECT_EQ(before.container, "");
  EXPECT_EQ(before.index, 0);  // the dragged row is not counted
  DropTarget after = At(TabItem(dragged), 1, 90);
  EXPECT_EQ(after.kind, DropKind::kAfter);
  EXPECT_EQ(after.index, 1);
  EXPECT_EQ(after.anchor_id, folder);
}

TEST_F(DropTargetTest, TheMiddleSixtyPercentMeansIntoTheFolder) {
  const std::string dragged = Tab("", 0);
  const std::string folder = Folder("", 1, Section::kToday, false);
  Tab(folder, 0);
  Tab(folder, 1);
  for (int percent : {20, 50, 79}) {
    DropTarget t = At(TabItem(dragged), 1, percent);
    EXPECT_EQ(t.kind, DropKind::kInto) << percent;
    EXPECT_EQ(t.container, folder);
    EXPECT_EQ(t.index, 2);  // the end of a folder that holds two
  }
}

TEST_F(DropTargetTest, AnExpandedFoldersBottomEdgeMeansFirstChild) {
  const std::string dragged = Tab("", 0);
  const std::string folder = Folder("", 1, Section::kToday, true);
  Tab(folder, 0);
  DropTarget t = At(TabItem(dragged), 1, 90);
  EXPECT_EQ(t.kind, DropKind::kFirstChild);
  EXPECT_EQ(t.container, folder);
  EXPECT_EQ(t.index, 0);
  // The top edge of an open folder is still "before" it.
  EXPECT_EQ(At(TabItem(dragged), 1, 5).kind, DropKind::kBefore);
}

TEST_F(DropTargetTest, ATabRowSplitsInHalves) {
  const std::string a = Tab("", 0);
  const std::string b = Tab("", 1);
  const std::string dragged = Tab("", 2);
  EXPECT_EQ(At(TabItem(dragged), 1, 30).kind, DropKind::kBefore);
  EXPECT_EQ(At(TabItem(dragged), 1, 30).index, 1);
  DropTarget lower = At(TabItem(dragged), 1, 70);
  EXPECT_EQ(lower.kind, DropKind::kAfter);
  EXPECT_EQ(lower.index, 2);
  EXPECT_EQ(lower.anchor_id, b);
  (void)a;
}

TEST_F(DropTargetTest, BelowTheLastRowIsTheEndOfTheSection) {
  Tab("", 0);
  const std::string folder = Folder("", 1);
  const std::string inner = Tab(folder, 0);
  const std::string dragged = Tab("", 2);
  DropTarget t = ComputeDropTarget(model_, 10, kTop + 5 * kRowHeight,
                                   Geometry(), TabItem(dragged));
  EXPECT_EQ(t.kind, DropKind::kAfter);
  EXPECT_EQ(t.container, "");
  EXPECT_EQ(t.index, 2);  // t0 and the folder, not the dragged row
  (void)inner;
}

TEST_F(DropTargetTest, AnEmptySectionTakesTheDropAtIndexZero) {
  const std::string dragged = Tab("", 0);
  DropTarget t = ComputeDropTarget(model_, 10, 300, Geometry(), TabItem(dragged));
  EXPECT_EQ(t.kind, DropKind::kBefore);
  EXPECT_EQ(t.section, Section::kPinned);
  EXPECT_EQ(t.index, 0);
}

TEST_F(DropTargetTest, ADropPastFiveLevelsIsRefusedAsTooDeep) {
  std::string parent;
  std::string level5;
  for (int i = 1; i <= 5; ++i) {
    parent = Folder(parent, 0);
    level5 = parent;
  }
  const std::string deep = Folder("", 1);  // a folder with no children, height 1
  const std::string shallow_tab = Tab("", 2);
  // A folder into the level-5 folder: 5 + 1 > 5.
  DropTarget t = ComputeDropTarget(model_, 10, kTop + 4 * kRowHeight + 50,
                                   Geometry(), FolderItem(deep));
  EXPECT_EQ(t.kind, DropKind::kRefused);
  EXPECT_EQ(t.reason, RefusalReason::kTooDeep);
  // A tab still goes into a level-5 folder.
  DropTarget ok = ComputeDropTarget(model_, 10, kTop + 4 * kRowHeight + 50,
                                    Geometry(), TabItem(shallow_tab));
  EXPECT_EQ(ok.kind, DropKind::kInto);
  EXPECT_EQ(ok.container, level5);
  // Next to the level-5 folder (inside level 4) a folder of height 1 fits.
  EXPECT_EQ(At(FolderItem(deep), 4, 5).kind, DropKind::kBefore);
}

TEST_F(DropTargetTest, AFolderDroppedIntoItsOwnChildIsRefused) {
  const std::string outer = Folder("", 0);
  const std::string inner = Folder(outer, 0);
  Tab(inner, 0);
  // Rows: outer, inner, tab. Middle of `inner`: into itself's descendant.
  DropTarget t = At(FolderItem(outer), 1, 50);
  EXPECT_EQ(t.kind, DropKind::kRefused);
  EXPECT_EQ(t.reason, RefusalReason::kIntoItself);
  // A row inside the dragged folder is just as much a child.
  EXPECT_EQ(At(FolderItem(outer), 2, 30).kind, DropKind::kRefused);
  // Over its own row nothing changes.
  EXPECT_EQ(At(FolderItem(outer), 0, 50).kind, DropKind::kBefore);
}

TEST_F(DropTargetTest, TheThirteenthFavoriteIsRefused) {
  for (int i = 0; i < 12; ++i) {
    Tab("", i, Section::kFavorite);
  }
  const std::string dragged = Tab("", 0);
  DropGeometry g = Geometry();
  g.favorites_area = {0, 2000, 400, 100};
  DropTarget t = ComputeDropTarget(model_, 10, 2050, g, TabItem(dragged));
  EXPECT_EQ(t.kind, DropKind::kRefused);
  EXPECT_EQ(t.reason, RefusalReason::kFavoritesFull);
}

TEST_F(DropTargetTest, ATwelfthFavoriteAndAFavoriteMovingInsideAreAccepted) {
  std::vector<std::string> favorites;
  for (int i = 0; i < 11; ++i) {
    favorites.push_back(Tab("", i, Section::kFavorite));
  }
  const std::string dragged = Tab("", 0);
  DropGeometry g = Geometry();
  g.favorites_area = {0, 2000, 400, 100};
  EXPECT_EQ(ComputeDropTarget(model_, 10, 2050, g, TabItem(dragged)).kind,
            DropKind::kFavoritesSlot);
  // At 12, a Favorite that is already one still moves.
  Tab("", 11, Section::kFavorite);
  EXPECT_EQ(ComputeDropTarget(model_, 10, 2050, g, TabItem(favorites[0])).kind,
            DropKind::kFavoritesSlot);
}

TEST_F(DropTargetTest, TheFavoritesSlotFollowsReadingOrderInTwoDimensions) {
  std::vector<std::string> ids;
  for (int i = 0; i < 6; ++i) {
    ids.push_back(Tab("", i, Section::kFavorite));
  }
  const std::string dragged = Tab("", 0);
  DropGeometry g = Geometry();
  g.favorites_area = {0, 2000, 400, 200};
  // Four columns of 100 x 100: tiles 0..3 in the first row, 4..5 below.
  for (int i = 0; i < 6; ++i) {
    g.tiles.push_back({(i % 4) * 100, 2000 + (i / 4) * 100, 100, 100});
    g.tile_ids.push_back(ids[i]);
  }
  auto slot = [&](int x, int y) {
    return ComputeDropTarget(model_, x, y, g, TabItem(dragged)).index;
  };
  EXPECT_EQ(slot(10, 2050), 0);
  EXPECT_EQ(slot(90, 2050), 1);   // past the middle of tile 0
  EXPECT_EQ(slot(390, 2050), 4);  // end of the first row
  EXPECT_EQ(slot(10, 2150), 4);   // start of the second row
  EXPECT_EQ(slot(390, 2150), 6);
}

TEST_F(DropTargetTest, AFolderDoesNotGoIntoFavorites) {
  Tab("", 0, Section::kFavorite);
  const std::string folder = Folder("", 0);
  DropGeometry g = Geometry();
  g.favorites_area = {0, 2000, 400, 100};
  DropTarget t = ComputeDropTarget(model_, 10, 2050, g, FolderItem(folder));
  EXPECT_EQ(t.kind, DropKind::kRefused);
  EXPECT_EQ(t.reason, RefusalReason::kFolderInFavorites);
}

TEST_F(DropTargetTest, DroppingIntoTheFolderTheTabIsInDoesNotCountTheTab) {
  const std::string folder = Folder("", 0, Section::kToday, false);
  const std::string a = Tab(folder, 0);
  Tab(folder, 1);
  DropTarget t = At(TabItem(a), 0, 50);
  EXPECT_EQ(t.kind, DropKind::kInto);
  EXPECT_EQ(t.index, 1);
}

TEST_F(DropTargetTest, ARowOfPinnedGivesThePinnedSection) {
  const std::string pinned = Tab("", 0, Section::kPinned);
  const std::string dragged = Tab("", 0);
  DropTarget t = ComputeDropTarget(model_, 10, 20, Geometry(), TabItem(dragged));
  EXPECT_EQ(t.section, Section::kPinned);
  EXPECT_EQ(t.kind, DropKind::kBefore);
  EXPECT_EQ(t.anchor_id, pinned);
}

// --- The dividers between the sections ------------------------------------

TEST_F(DropTargetTest, TheDividerUnderAnEmptyPinnedPutsTheItemInPinned) {
  const std::string dragged = Tab("", 0);
  DropGeometry g = Geometry();
  g.pinned = DropList();  // An empty Pinned list has no height at all.
  g.pinned_divider = {0, 500, 400, 8};
  DropTarget t = ComputeDropTarget(model_, 10, 504, g, TabItem(dragged));
  EXPECT_EQ(t.kind, DropKind::kBefore);
  EXPECT_EQ(t.section, Section::kPinned);
  EXPECT_EQ(t.container, "");
  EXPECT_EQ(t.index, 0);
  EXPECT_TRUE(t.on_divider);
}

TEST_F(DropTargetTest, TheDividerUnderPinnedMeansTheEndOfPinned) {
  Tab("", 0, Section::kPinned);
  const std::string last = Tab("", 1, Section::kPinned);
  const std::string dragged = Tab("", 0);
  DropGeometry g = Geometry();
  g.pinned_divider = {0, 500, 400, 8};
  DropTarget t = ComputeDropTarget(model_, 10, 504, g, TabItem(dragged));
  EXPECT_EQ(t.kind, DropKind::kAfter);
  EXPECT_EQ(t.section, Section::kPinned);
  EXPECT_EQ(t.index, 2);
  EXPECT_EQ(t.anchor_id, last);
  EXPECT_TRUE(t.on_divider);
}

TEST_F(DropTargetTest, ADraggedPinnedTabDoesNotCountItselfAtTheDivider) {
  const std::string first = Tab("", 0, Section::kPinned);
  Tab("", 1, Section::kPinned);
  DropGeometry g = Geometry();
  g.pinned_divider = {0, 500, 400, 8};
  DropTarget t = ComputeDropTarget(model_, 10, 504, g, TabItem(first));
  EXPECT_EQ(t.index, 1);
}

TEST_F(DropTargetTest, TheDividerUnderFavoritesPutsATabAtTheEndOfFavorites) {
  const std::string dragged = Tab("", 0);
  DropGeometry g = Geometry();
  g.favorites_divider = {0, 400, 400, 8};
  DropTarget t = ComputeDropTarget(model_, 10, 404, g, TabItem(dragged));
  EXPECT_EQ(t.kind, DropKind::kFavoritesSlot);
  EXPECT_EQ(t.section, Section::kFavorite);
  EXPECT_EQ(t.index, 0);
  EXPECT_TRUE(t.on_divider);
}

TEST_F(DropTargetTest, TheFavoritesDividerCountsTheTilesThatAreThere) {
  std::vector<std::string> ids;
  for (int i = 0; i < 3; ++i) {
    ids.push_back(Tab("", i, Section::kFavorite));
  }
  const std::string dragged = Tab("", 0);
  DropGeometry g = Geometry();
  g.favorites_area = {0, 300, 400, 100};
  for (int i = 0; i < 3; ++i) {
    g.tiles.push_back({i * 100, 300, 100, 100});
    g.tile_ids.push_back(ids[i]);
  }
  g.favorites_divider = {0, 400, 400, 8};
  EXPECT_EQ(ComputeDropTarget(model_, 10, 404, g, TabItem(dragged)).index, 3);
  // A Favorite dragged to its own divider leaves its own tile out.
  EXPECT_EQ(ComputeDropTarget(model_, 10, 404, g, TabItem(ids[0])).index, 2);
}

TEST_F(DropTargetTest, TheFavoritesDividerRefusesAThirteenthAndAFolder) {
  for (int i = 0; i < 12; ++i) {
    Tab("", i, Section::kFavorite);
  }
  const std::string dragged = Tab("", 0);
  const std::string folder = Folder("", 1);
  DropGeometry g = Geometry();
  g.favorites_divider = {0, 400, 400, 8};
  DropTarget full = ComputeDropTarget(model_, 10, 404, g, TabItem(dragged));
  EXPECT_EQ(full.kind, DropKind::kRefused);
  EXPECT_EQ(full.reason, RefusalReason::kFavoritesFull);
  EXPECT_TRUE(full.on_divider);
  DropTarget f = ComputeDropTarget(model_, 10, 404, g, FolderItem(folder));
  EXPECT_EQ(f.reason, RefusalReason::kFolderInFavorites);
  EXPECT_TRUE(f.on_divider);
}

TEST_F(DropTargetTest, AFolderCanBeDroppedOnThePinnedDivider) {
  const std::string folder = Folder("", 0);
  DropGeometry g = Geometry();
  g.pinned_divider = {0, 500, 400, 8};
  DropTarget t = ComputeDropTarget(model_, 10, 504, g, FolderItem(folder));
  EXPECT_EQ(t.section, Section::kPinned);
  EXPECT_NE(t.kind, DropKind::kRefused);
}

TEST_F(DropTargetTest, ADividerWinsOverTheRowItOverlapsAndAZeroHeightOneIsIgnored) {
  Tab("", 0);
  const std::string dragged = Tab("", 1);
  DropGeometry g = Geometry();
  // The divider lies over the top of the first Today row.
  g.pinned_divider = {0, kTop, 400, 8};
  EXPECT_EQ(ComputeDropTarget(model_, 10, kTop + 2, g, TabItem(dragged)).section,
            Section::kPinned);
  g.pinned_divider = {0, kTop, 400, 0};
  DropTarget t = ComputeDropTarget(model_, 10, kTop + 2, g, TabItem(dragged));
  EXPECT_EQ(t.section, Section::kToday);
  EXPECT_FALSE(t.on_divider);
}

}  // namespace aerie_sidebar
