// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_sidebar/aerie_sidebar_folder_toggle.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace aerie_sidebar {
namespace {

struct FakeRow {
  int id = 0;
};

using Toggle = FolderToggle<FakeRow>;

TEST(AerieSidebarFolderToggleTest, NothingIsPendingAtFirst) {
  Toggle toggle;
  EXPECT_FALSE(toggle.active());
  EXPECT_FALSE(toggle.opening());
  EXPECT_FALSE(toggle.shutting());
  EXPECT_FALSE(toggle.is_for(""));
  EXPECT_TRUE(toggle.Take().revealed.empty());
}

TEST(AerieSidebarFolderToggleTest, BeginSetsAnOpeningToggle) {
  Toggle toggle;
  toggle.Begin("trips", /*expand=*/true);
  EXPECT_TRUE(toggle.active());
  EXPECT_TRUE(toggle.opening());
  EXPECT_FALSE(toggle.shutting());
  EXPECT_EQ("trips", toggle.pending().folder_id);
  EXPECT_TRUE(toggle.pending().expand);
  EXPECT_TRUE(toggle.is_for("trips"));
  EXPECT_FALSE(toggle.is_for("work"));
}

TEST(AerieSidebarFolderToggleTest, BeginSetsAShuttingToggle) {
  Toggle toggle;
  toggle.Begin("trips", /*expand=*/false);
  EXPECT_TRUE(toggle.active());
  EXPECT_FALSE(toggle.opening());
  EXPECT_TRUE(toggle.shutting());
}

TEST(AerieSidebarFolderToggleTest, EndClearsTheToggle) {
  Toggle toggle;
  toggle.Begin("trips", true);
  toggle.End();
  EXPECT_FALSE(toggle.active());
  EXPECT_FALSE(toggle.opening());
  EXPECT_FALSE(toggle.is_for("trips"));
  EXPECT_TRUE(toggle.pending().folder_id.empty());
}

TEST(AerieSidebarFolderToggleTest, TakeHandsOverWhatTheSyncFoundAndClearsIt) {
  Toggle toggle;
  FakeRow a{1}, b{2}, c{3};
  toggle.Begin("trips", true);
  toggle.NoteRevealed(&a);
  toggle.NoteRevealed(&b);
  toggle.NoteCollapsing(&c);

  Toggle::Outcome found = toggle.Take();
  EXPECT_TRUE(found.toggle.active);
  EXPECT_EQ("trips", found.toggle.folder_id);
  EXPECT_TRUE(found.toggle.expand);
  ASSERT_EQ(2u, found.revealed.size());
  EXPECT_EQ(&a, found.revealed[0].get());
  EXPECT_EQ(&b, found.revealed[1].get());
  ASSERT_EQ(1u, found.collapsing.size());
  EXPECT_EQ(&c, found.collapsing[0].get());

  // A second take finds nothing, and the toggle is still pending until End.
  Toggle::Outcome again = toggle.Take();
  EXPECT_TRUE(again.revealed.empty());
  EXPECT_TRUE(again.collapsing.empty());
  EXPECT_TRUE(again.toggle.active);
  EXPECT_TRUE(toggle.active());
}

TEST(AerieSidebarFolderToggleTest, TakeWithNoToggleReportsNotActive) {
  Toggle toggle;
  EXPECT_FALSE(toggle.Take().toggle.active);
  toggle.Begin("trips", true);
  toggle.End();
  EXPECT_FALSE(toggle.Take().toggle.active);
}

TEST(AerieSidebarFolderToggleTest, ForgetCollapsingKeepsTheRevealedRows) {
  Toggle toggle;
  FakeRow a{1}, b{2};
  toggle.Begin("trips", false);
  toggle.NoteRevealed(&a);
  toggle.NoteCollapsing(&b);
  toggle.ForgetCollapsing();
  Toggle::Outcome found = toggle.Take();
  EXPECT_EQ(1u, found.revealed.size());
  EXPECT_TRUE(found.collapsing.empty());
}

TEST(AerieSidebarFolderToggleTest, ASecondBeginReplacesTheFirst) {
  Toggle toggle;
  toggle.Begin("trips", true);
  toggle.Begin("work", false);
  EXPECT_TRUE(toggle.is_for("work"));
  EXPECT_FALSE(toggle.is_for("trips"));
  EXPECT_TRUE(toggle.shutting());
}

TEST(AerieSidebarFolderToggleTest, RowsNotTakenSurviveEndAndBegin) {
  // The pending toggle and the rows a sync found are separate: ending the
  // toggle does not drop rows that nobody took.
  Toggle toggle;
  FakeRow a{1};
  toggle.Begin("trips", true);
  toggle.NoteRevealed(&a);
  toggle.End();
  toggle.Begin("trips", true);
  EXPECT_EQ(1u, toggle.Take().revealed.size());
}

}  // namespace
}  // namespace aerie_sidebar
