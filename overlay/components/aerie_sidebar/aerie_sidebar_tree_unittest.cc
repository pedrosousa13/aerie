// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_sidebar/aerie_sidebar_tree.h"

#include <vector>

#include "testing/gtest/include/gtest/gtest.h"

namespace aerie_sidebar {

namespace {

using aerie_tab_model::TreeRow;

TreeRow Tab(const std::string& id, const std::string& parent, int level) {
  TreeRow row;
  row.id = id;
  row.parent = parent;
  row.level = level;
  return row;
}

TreeRow Folder(const std::string& id,
               const std::string& parent,
               int level,
               bool expanded,
               int items = 1) {
  TreeRow row;
  row.is_folder = true;
  row.id = id;
  row.parent = parent;
  row.level = level;
  row.expanded = expanded;
  row.item_count = items;
  return row;
}

// 0 t0 | 1 F (open) | 2   t1 | 3   G (shut) | 4 t2
std::vector<TreeRow> Rows() {
  return {Tab("t0", "", 0), Folder("F", "", 0, true, 2),
          Tab("t1", "F", 1), Folder("G", "F", 1, false), Tab("t2", "", 0)};
}

}  // namespace

TEST(HandleTreeKeyTest, ArrowsHomeAndEndMoveTheStopAndStopAtTheEnds) {
  const std::vector<TreeRow> rows = Rows();
  EXPECT_EQ(HandleTreeKey(rows, 0, ListKey::kUp).focus_index, 0);
  EXPECT_EQ(HandleTreeKey(rows, 1, ListKey::kDown).focus_index, 2);
  EXPECT_EQ(HandleTreeKey(rows, 4, ListKey::kDown).focus_index, 4);
  EXPECT_EQ(HandleTreeKey(rows, 3, ListKey::kHome).focus_index, 0);
  EXPECT_EQ(HandleTreeKey(rows, 1, ListKey::kEnd).focus_index, 4);
  EXPECT_EQ(HandleTreeKey({}, 0, ListKey::kDown).focus_index, -1);
}

TEST(HandleTreeKeyTest, RightOpensAShutFolderAndThenMovesToTheFirstChild) {
  const std::vector<TreeRow> rows = Rows();
  TreeKeyResult shut = HandleTreeKey(rows, 3, ListKey::kRight);
  EXPECT_EQ(shut.toggle_folder_id, "G");
  EXPECT_TRUE(shut.expand);
  EXPECT_EQ(shut.focus_index, 3);
  TreeKeyResult open = HandleTreeKey(rows, 1, ListKey::kRight);
  EXPECT_TRUE(open.toggle_folder_id.empty());
  EXPECT_EQ(open.focus_index, 2);
  // On a tab, and on an open folder with no child row, nothing.
  EXPECT_EQ(HandleTreeKey(rows, 0, ListKey::kRight).focus_index, 0);
  EXPECT_TRUE(HandleTreeKey(rows, 0, ListKey::kRight).toggle_folder_id.empty());
  std::vector<TreeRow> empty_open = {Folder("E", "", 0, true, 0), Tab("x", "", 0)};
  EXPECT_EQ(HandleTreeKey(empty_open, 0, ListKey::kRight).focus_index, 0);
}

TEST(HandleTreeKeyTest, LeftShutsAnOpenFolderElseMovesToTheParent) {
  const std::vector<TreeRow> rows = Rows();
  TreeKeyResult open = HandleTreeKey(rows, 1, ListKey::kLeft);
  EXPECT_EQ(open.toggle_folder_id, "F");
  EXPECT_FALSE(open.expand);
  EXPECT_EQ(open.focus_index, 1);
  // A tab and a shut folder go to their parent.
  EXPECT_EQ(HandleTreeKey(rows, 2, ListKey::kLeft).focus_index, 1);
  EXPECT_TRUE(HandleTreeKey(rows, 2, ListKey::kLeft).toggle_folder_id.empty());
  EXPECT_EQ(HandleTreeKey(rows, 3, ListKey::kLeft).focus_index, 1);
  // At the top level there is no parent.
  EXPECT_EQ(HandleTreeKey(rows, 0, ListKey::kLeft).focus_index, 0);
  EXPECT_EQ(HandleTreeKey(rows, 4, ListKey::kLeft).focus_index, 4);
}

TEST(HandleTreeKeyTest, ToggleFlipsAFolderAndDoesNothingOnATab) {
  const std::vector<TreeRow> rows = Rows();
  TreeKeyResult a = HandleTreeKey(rows, 1, ListKey::kToggle);
  EXPECT_EQ(a.toggle_folder_id, "F");
  EXPECT_FALSE(a.expand);
  TreeKeyResult b = HandleTreeKey(rows, 3, ListKey::kToggle);
  EXPECT_EQ(b.toggle_folder_id, "G");
  EXPECT_TRUE(b.expand);
  EXPECT_TRUE(HandleTreeKey(rows, 0, ListKey::kToggle).toggle_folder_id.empty());
}

TEST(HandleTreeKeyTest, DeleteClosesATabButNeverAFolder) {
  const std::vector<TreeRow> rows = Rows();
  TreeKeyResult tab = HandleTreeKey(rows, 2, ListKey::kDelete);
  EXPECT_TRUE(tab.close_focused);
  EXPECT_EQ(tab.focus_index, 2);
  TreeKeyResult last = HandleTreeKey(rows, 4, ListKey::kDelete);
  EXPECT_TRUE(last.close_focused);
  EXPECT_EQ(last.focus_index, 3);
  TreeKeyResult folder = HandleTreeKey(rows, 1, ListKey::kDelete);
  EXPECT_FALSE(folder.close_focused);
  EXPECT_EQ(folder.focus_index, 1);
}

TEST(StopAfterCollapseTest, FocusInsideTheFolderMovesToTheFolderRow) {
  const std::vector<TreeRow> rows = Rows();
  EXPECT_EQ(StopAfterCollapse(rows, "F", 2), 1);
  EXPECT_EQ(StopAfterCollapse(rows, "F", 3), 1);
  EXPECT_EQ(StopAfterCollapse(rows, "F", 4), -1);
  EXPECT_EQ(StopAfterCollapse(rows, "F", 0), -1);
  EXPECT_EQ(StopAfterCollapse(rows, "F", 1), -1);
  EXPECT_EQ(StopAfterCollapse(rows, "nope", 2), -1);
}

TEST(RevealStaggerTest, TheFirstSixAreFifteenMsApartAndTheRestShareTheLastDelay) {
  EXPECT_EQ(RevealStaggerDelayMs(0, false), 0);
  EXPECT_EQ(RevealStaggerDelayMs(1, false), 15);
  EXPECT_EQ(RevealStaggerDelayMs(5, false), 75);
  EXPECT_EQ(RevealStaggerDelayMs(6, false), 75);
  EXPECT_EQ(RevealStaggerDelayMs(200, false), 75);
}

TEST(RevealStaggerTest, ReducedMotionHasNoStagger) {
  for (int i = 0; i < 10; ++i) {
    EXPECT_EQ(RevealStaggerDelayMs(i, true), 0);
  }
}

}  // namespace aerie_sidebar
