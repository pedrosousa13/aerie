// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_sidebar/aerie_sidebar_recent_tabs.h"

#include <algorithm>
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

// ---- More tabs than the overlay has room for -----------------------------

Ids Eight() { return Ids{1, 2, 3, 4, 5, 6, 7, 8}; }

TEST(RecentTabSessionTest, TheVisibleCardsAreTheFirstOnesAtTheStart) {
  RecentTabSession session;
  ASSERT_TRUE(session.Begin(Eight(), /*backwards=*/false, /*max_visible=*/5));
  EXPECT_EQ(session.VisibleCards(), (Ids{1, 2, 3, 4, 5}));
  EXPECT_EQ(session.first_visible(), 0u);
  EXPECT_EQ(session.visible_selected_index(), 1u);
  // Every tab is still in the session.
  EXPECT_EQ(session.cards().size(), 8u);
}

TEST(RecentTabSessionTest, SteppingPastTheLastVisibleCardSlidesTheWindow) {
  RecentTabSession session;
  ASSERT_TRUE(session.Begin(Eight(), false, 5));
  for (int i = 0; i < 5; ++i) {
    session.StepForward();
  }
  // From the second card, five steps reach the seventh tab.
  EXPECT_EQ(session.selected(), 7);
  EXPECT_EQ(session.VisibleCards(), (Ids{3, 4, 5, 6, 7}));
  EXPECT_EQ(session.visible_selected_index(), 4u);
}

TEST(RecentTabSessionTest, SteppingBackSlidesTheWindowBack) {
  RecentTabSession session;
  ASSERT_TRUE(session.Begin(Eight(), false, 5));
  for (int i = 0; i < 6; ++i) {
    session.StepForward();  // the last tab, 8
  }
  EXPECT_EQ(session.VisibleCards(), (Ids{4, 5, 6, 7, 8}));
  for (int i = 0; i < 4; ++i) {
    session.StepBackward();  // 4, the first visible card
  }
  EXPECT_EQ(session.selected(), 4);
  EXPECT_EQ(session.VisibleCards(), (Ids{4, 5, 6, 7, 8}));
  session.StepBackward();  // 3: slides
  EXPECT_EQ(session.VisibleCards(), (Ids{3, 4, 5, 6, 7}));
  EXPECT_EQ(session.visible_selected_index(), 0u);
}

TEST(RecentTabSessionTest, WrappingMovesTheWindowToTheOtherEnd) {
  RecentTabSession session;
  ASSERT_TRUE(session.Begin(Eight(), false, 5));
  for (int i = 0; i < 7; ++i) {
    session.StepForward();  // 8, then back to 1
  }
  EXPECT_EQ(session.selected(), 1);
  EXPECT_EQ(session.VisibleCards(), (Ids{1, 2, 3, 4, 5}));
  session.StepBackward();  // wraps to 8
  EXPECT_EQ(session.selected(), 8);
  EXPECT_EQ(session.VisibleCards(), (Ids{4, 5, 6, 7, 8}));
}

TEST(RecentTabSessionTest, CtrlShiftTabAtTheStartShowsTheLeastRecentTabs) {
  RecentTabSession session;
  ASSERT_TRUE(session.Begin(Eight(), /*backwards=*/true, 5));
  EXPECT_EQ(session.selected(), 8);
  EXPECT_EQ(session.VisibleCards(), (Ids{4, 5, 6, 7, 8}));
}

TEST(RecentTabSessionTest, AClosedTabKeepsTheSelectionInTheWindow) {
  RecentTabSession session;
  ASSERT_TRUE(session.Begin(Eight(), false, 5));
  for (int i = 0; i < 6; ++i) {
    session.StepForward();  // 8, the window is 4..8
  }
  session.TabRemoved(8);  // the selection moves to the new last card, 7
  EXPECT_EQ(session.selected(), 7);
  EXPECT_EQ(session.VisibleCards().size(), 5u);
  const Ids visible = session.VisibleCards();
  EXPECT_NE(std::find(visible.begin(), visible.end(), 7), visible.end());
}

TEST(RecentTabSessionTest, FewerTabsThanRoomShowThemAll) {
  RecentTabSession session;
  ASSERT_TRUE(session.Begin(Ids{1, 2, 3}, false, 5));
  EXPECT_EQ(session.VisibleCards(), (Ids{1, 2, 3}));
  session.StepForward();
  session.StepForward();
  EXPECT_EQ(session.first_visible(), 0u);
}

}  // namespace aerie_sidebar
