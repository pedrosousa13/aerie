// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_sidebar/aerie_sidebar_recent_tabs.h"

#include <vector>

#include "testing/gtest/include/gtest/gtest.h"

namespace aerie_sidebar {

namespace {

using Ids = std::vector<int>;

// Tabs 1, 2, 3 and 4 were opened in that order, each one in front, so 4 is the
// active one and the order is 4 3 2 1.
RecentTabs Four() {
  RecentTabs recent;
  for (int id : {1, 2, 3, 4}) {
    recent.TabAdded(id, /*active=*/true);
  }
  return recent;
}

}  // namespace

TEST(RecentTabsTest, ActivatingATabMovesItToTheFront) {
  RecentTabs recent = Four();
  recent.TabActivated(2);
  EXPECT_EQ(recent.Order(), (Ids{2, 4, 3, 1}));
  recent.TabActivated(3);
  EXPECT_EQ(recent.Order(), (Ids{3, 2, 4, 1}));
  // Activating the tab that is in front changes nothing.
  recent.TabActivated(3);
  EXPECT_EQ(recent.Order(), (Ids{3, 2, 4, 1}));
}

TEST(RecentTabsTest, ANewActiveTabIsInFrontAndABackgroundTabIsLast) {
  RecentTabs recent = Four();
  recent.TabAdded(5, /*active=*/false);
  EXPECT_EQ(recent.Order(), (Ids{4, 3, 2, 1, 5}));
  recent.TabAdded(6, /*active=*/true);
  EXPECT_EQ(recent.Order(), (Ids{6, 4, 3, 2, 1, 5}));
}

TEST(RecentTabsTest, ARestoredWindowKeepsTheTabStripOrderBehindTheActiveTab) {
  // A restored window: the tabs come in strip order, then the active one.
  RecentTabs recent;
  for (int id : {10, 11, 12, 13}) {
    recent.TabAdded(id, /*active=*/false);
  }
  recent.TabActivated(12);
  EXPECT_EQ(recent.Order(), (Ids{12, 10, 11, 13}));
}

TEST(RecentTabsTest, AClosedTabDropsOutAndTheRestKeepTheirOrder) {
  RecentTabs recent = Four();
  recent.TabActivated(2);
  recent.TabRemoved(4);
  EXPECT_EQ(recent.Order(), (Ids{2, 3, 1}));
  // A tab that is not known is ignored.
  recent.TabRemoved(99);
  EXPECT_EQ(recent.Order(), (Ids{2, 3, 1}));
}

TEST(RecentTabsTest, AddingATabTwiceKeepsOneEntry) {
  RecentTabs recent = Four();
  recent.TabAdded(2, /*active=*/false);
  EXPECT_EQ(recent.Order(), (Ids{4, 3, 2, 1}));
}

TEST(RecentTabsTest, ActivatingAnUnknownTabAddsItInFront) {
  RecentTabs recent = Four();
  recent.TabActivated(7);
  EXPECT_EQ(recent.Order(), (Ids{7, 4, 3, 2, 1}));
}

TEST(RecentTabSessionTest, ThePreviousTabIsSelectedFirst) {
  RecentTabs recent = Four();
  recent.TabActivated(2);  // 2 4 3 1
  RecentTabSession session;
  ASSERT_TRUE(session.Begin(recent.Order(), /*backwards=*/false));
  EXPECT_EQ(session.cards(), (Ids{2, 4, 3, 1}));
  EXPECT_EQ(session.selected(), 4);
  EXPECT_EQ(session.selected_index(), 1u);
}

TEST(RecentTabSessionTest, StepsGoAlongTheOrderAndWrapAround) {
  RecentTabSession session;
  ASSERT_TRUE(session.Begin(Ids{1, 2, 3}, /*backwards=*/false));
  EXPECT_EQ(session.selected(), 2);
  session.StepForward();
  EXPECT_EQ(session.selected(), 3);
  session.StepForward();
  EXPECT_EQ(session.selected(), 1);
  session.StepForward();
  EXPECT_EQ(session.selected(), 2);
}

TEST(RecentTabSessionTest, StepBackGoesTheOtherWayAndWrapsAround) {
  RecentTabSession session;
  ASSERT_TRUE(session.Begin(Ids{1, 2, 3}, /*backwards=*/false));
  session.StepBackward();
  EXPECT_EQ(session.selected(), 1);
  session.StepBackward();
  EXPECT_EQ(session.selected(), 3);
}

TEST(RecentTabSessionTest, CtrlShiftTabAtTheStartSelectsTheLeastRecentTab) {
  RecentTabSession session;
  ASSERT_TRUE(session.Begin(Ids{1, 2, 3, 4}, /*backwards=*/true));
  EXPECT_EQ(session.selected(), 4);
  session.StepBackward();
  EXPECT_EQ(session.selected(), 3);
  session.StepForward();
  EXPECT_EQ(session.selected(), 4);
}

TEST(RecentTabSessionTest, ALoneTabHasNothingToSwitchTo) {
  RecentTabSession session;
  EXPECT_FALSE(session.Begin(Ids{1}, /*backwards=*/false));
  EXPECT_FALSE(session.active());
  EXPECT_FALSE(session.Begin(Ids{}, /*backwards=*/true));
}

TEST(RecentTabSessionTest, TwoTabsSwapBackAndForth) {
  RecentTabSession session;
  ASSERT_TRUE(session.Begin(Ids{5, 6}, /*backwards=*/false));
  EXPECT_EQ(session.selected(), 6);
  session.StepForward();
  EXPECT_EQ(session.selected(), 5);
}

TEST(RecentTabSessionTest, TheCardsStayWhileTheSessionIsOpen) {
  // The order is a snapshot: the tab that gets focus later does not reorder
  // the cards under the user's eyes.
  RecentTabs recent = Four();
  RecentTabSession session;
  ASSERT_TRUE(session.Begin(recent.Order(), false));
  recent.TabActivated(1);
  EXPECT_EQ(session.cards(), (Ids{4, 3, 2, 1}));
}

TEST(RecentTabSessionTest, ATabThatClosesLeavesTheCardsAndKeepsTheSelection) {
  RecentTabSession session;
  ASSERT_TRUE(session.Begin(Ids{1, 2, 3, 4}, false));
  session.StepForward();  // 3
  session.TabRemoved(1);
  EXPECT_EQ(session.cards(), (Ids{2, 3, 4}));
  EXPECT_EQ(session.selected(), 3);
  // The selected one closes: the next card takes its place.
  session.TabRemoved(3);
  EXPECT_EQ(session.cards(), (Ids{2, 4}));
  EXPECT_EQ(session.selected(), 4);
  // The last card closes while selected: the new last one is selected.
  session.TabRemoved(4);
  EXPECT_EQ(session.cards(), (Ids{2}));
  EXPECT_EQ(session.selected(), 2);
}

TEST(RecentTabSessionTest, EndingTheSessionClearsIt) {
  RecentTabSession session;
  ASSERT_TRUE(session.Begin(Ids{1, 2}, false));
  EXPECT_TRUE(session.active());
  session.End();
  EXPECT_FALSE(session.active());
  EXPECT_TRUE(session.cards().empty());
}

}  // namespace aerie_sidebar
