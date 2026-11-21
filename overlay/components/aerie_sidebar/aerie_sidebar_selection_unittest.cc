// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_sidebar/aerie_sidebar_selection.h"

#include <string>
#include <vector>

#include "testing/gtest/include/gtest/gtest.h"

namespace aerie_sidebar {

namespace {

using aerie_tab_model::Section;
using Ids = std::vector<std::string>;

SelectionRow Tab(const std::string& id, Section section) {
  SelectionRow row;
  row.id = id;
  row.section = section;
  return row;
}

SelectionRow Folder(const std::string& id, Section section) {
  SelectionRow row = Tab(id, section);
  row.selectable = false;
  return row;
}

// Favorites: f0 f1 | Pinned: p0 F(folder) p1 p2 | Today: t0 t1 G(folder) t2 t3.
// The folders are rows too; only the tabs can be selected.
std::vector<SelectionRow> Rows() {
  return {Tab("f0", Section::kFavorite), Tab("f1", Section::kFavorite),
          Tab("p0", Section::kPinned),   Folder("F", Section::kPinned),
          Tab("p1", Section::kPinned),   Tab("p2", Section::kPinned),
          Tab("t0", Section::kToday),    Tab("t1", Section::kToday),
          Folder("G", Section::kToday),  Tab("t2", Section::kToday),
          Tab("t3", Section::kToday)};
}

class SelectionModelTest : public testing::Test {
 protected:
  void SetUp() override { model_.SetRows(Rows()); }
  SelectionModel model_;
};

TEST_F(SelectionModelTest, StartsEmpty) {
  EXPECT_TRUE(model_.empty());
  EXPECT_TRUE(model_.Selected().empty());
  EXPECT_FALSE(model_.IsSelected("t0"));
}

TEST_F(SelectionModelTest, ControlClickTogglesOneRowAtATime) {
  model_.Toggle("t0");
  model_.Toggle("t2");
  EXPECT_EQ(model_.Selected(), (Ids{"t0", "t2"}));
  model_.Toggle("t0");
  EXPECT_EQ(model_.Selected(), (Ids{"t2"}));
  model_.Toggle("t2");
  EXPECT_TRUE(model_.empty());
}

TEST_F(SelectionModelTest, SelectedListFollowsDisplayOrderNotClickOrder) {
  model_.Toggle("t3");
  model_.Toggle("t0");
  model_.Toggle("t2");
  EXPECT_EQ(model_.Selected(), (Ids{"t0", "t2", "t3"}));
}

TEST_F(SelectionModelTest, AFolderRowCannotBeSelected) {
  EXPECT_FALSE(model_.Toggle("G"));
  EXPECT_TRUE(model_.empty());
}

TEST_F(SelectionModelTest, ShiftClickSelectsTheRangeFromTheAnchor) {
  model_.Toggle("t0");
  model_.SelectRange("t3");
  // The folder row G sits inside the range and is skipped; the tabs on both
  // sides of it are in.
  EXPECT_EQ(model_.Selected(), (Ids{"t0", "t1", "t2", "t3"}));
}

TEST_F(SelectionModelTest, ShiftClickBackwardsAndAgainMovesTheFarEndOnly) {
  model_.Toggle("t2");
  model_.SelectRange("t0");
  EXPECT_EQ(model_.Selected(), (Ids{"t0", "t1", "t2"}));
  model_.SelectRange("t3");
  EXPECT_EQ(model_.Selected(), (Ids{"t2", "t3"}));
}

TEST_F(SelectionModelTest, ARangeAcrossAFolderTakesTheFolderTabsWhenOpen) {
  // p0 .. p2 spans the folder F; its child p1 is a visible row.
  model_.Toggle("p0");
  model_.SelectRange("p2");
  EXPECT_EQ(model_.Selected(), (Ids{"p0", "p1", "p2"}));
}

TEST_F(SelectionModelTest, ShiftClickWithoutAnAnchorSelectsThatRow) {
  model_.SelectRange("t1");
  EXPECT_EQ(model_.Selected(), (Ids{"t1"}));
}

TEST_F(SelectionModelTest, ARangeDoesNotCrossSections) {
  model_.Toggle("p2");
  model_.SelectRange("t1");
  // The anchor is in Pinned and the click is in Today: the click starts a
  // selection of its own.
  EXPECT_EQ(model_.Selected(), (Ids{"t1"}));
  model_.SelectRange("t3");
  EXPECT_EQ(model_.Selected(), (Ids{"t1", "t2", "t3"}));
}

TEST_F(SelectionModelTest, ControlClickInAnotherSectionStartsANewSelection) {
  model_.Toggle("f0");
  model_.Toggle("t0");
  EXPECT_EQ(model_.Selected(), (Ids{"t0"}));
  EXPECT_EQ(model_.section(), Section::kToday);
}

TEST_F(SelectionModelTest, ClearEmptiesTheSelectionAndForgetsTheAnchor) {
  model_.Toggle("t0");
  model_.Toggle("t1");
  model_.Clear();
  EXPECT_TRUE(model_.empty());
  // With no anchor a shift-click is a plain selection of one row.
  model_.SelectRange("t3");
  EXPECT_EQ(model_.Selected(), (Ids{"t3"}));
}

TEST_F(SelectionModelTest, SelectAllTakesTheTabsOfOneSectionOnly) {
  model_.SelectAll(Section::kPinned);
  EXPECT_EQ(model_.Selected(), (Ids{"p0", "p1", "p2"}));
  model_.SelectAll(Section::kToday);
  EXPECT_EQ(model_.Selected(), (Ids{"t0", "t1", "t2", "t3"}));
  model_.SelectAll(Section::kFavorite);
  EXPECT_EQ(model_.Selected(), (Ids{"f0", "f1"}));
}

TEST_F(SelectionModelTest, ShiftDownExtendsFromTheFocusedRow) {
  EXPECT_EQ(model_.Extend("t0", +1), "t1");
  EXPECT_EQ(model_.Selected(), (Ids{"t0", "t1"}));
  // The folder row is passed over.
  EXPECT_EQ(model_.Extend("t1", +1), "t2");
  EXPECT_EQ(model_.Selected(), (Ids{"t0", "t1", "t2"}));
}

TEST_F(SelectionModelTest, ShiftUpAfterShiftDownShrinksTheSelectionAgain) {
  model_.Extend("t0", +1);
  model_.Extend("t1", +1);
  EXPECT_EQ(model_.Extend("t2", -1), "t1");
  EXPECT_EQ(model_.Selected(), (Ids{"t0", "t1"}));
}

TEST_F(SelectionModelTest, ExtendingStopsAtTheEdgeOfTheSection) {
  EXPECT_EQ(model_.Extend("t3", +1), "");
  EXPECT_EQ(model_.Selected(), (Ids{"t3"}));
  EXPECT_EQ(model_.Extend("p0", -1), "");
  EXPECT_EQ(model_.Selected(), (Ids{"p0"}));
}

TEST_F(SelectionModelTest, ExtendingFromAnotherRowThanTheFarEndStartsOver) {
  model_.Toggle("t0");
  model_.Toggle("t3");
  EXPECT_EQ(model_.Extend("t1", +1), "t2");
  EXPECT_EQ(model_.Selected(), (Ids{"t1", "t2"}));
}

TEST_F(SelectionModelTest, RowsThatLeaveTheListLeaveTheSelection) {
  model_.SelectRange("t1");
  model_.SelectAll(Section::kToday);
  std::vector<SelectionRow> rows = Rows();
  // t1 closes.
  rows.erase(rows.begin() + 7);
  model_.SetRows(rows);
  EXPECT_EQ(model_.Selected(), (Ids{"t0", "t2", "t3"}));
}

TEST_F(SelectionModelTest, ClosingEveryRowEmptiesTheSelection) {
  model_.SelectAll(Section::kToday);
  model_.SetRows({Tab("f0", Section::kFavorite)});
  EXPECT_TRUE(model_.empty());
  EXPECT_FALSE(model_.section().has_value());
}

TEST_F(SelectionModelTest, TheAnchorSurvivesWhenOtherRowsLeave) {
  model_.Toggle("t0");
  std::vector<SelectionRow> rows = Rows();
  rows.erase(rows.begin() + 7);  // t1
  model_.SetRows(rows);
  model_.SelectRange("t3");
  EXPECT_EQ(model_.Selected(), (Ids{"t0", "t2", "t3"}));
}

TEST_F(SelectionModelTest, ARowThatLeavesAndComesBackIsNotSelectedAgain) {
  model_.Toggle("t0");
  model_.SetRows({Tab("t1", Section::kToday)});
  model_.SetRows(Rows());
  EXPECT_FALSE(model_.IsSelected("t0"));
  EXPECT_TRUE(model_.empty());
}

}  // namespace

}  // namespace aerie_sidebar
