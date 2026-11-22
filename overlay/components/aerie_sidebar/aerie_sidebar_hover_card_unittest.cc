// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_sidebar/aerie_sidebar_hover_card.h"

#include "testing/gtest/include/gtest/gtest.h"

// The placement and the text of the tab preview card (issue #270).
namespace aerie_sidebar {
namespace {

constexpr int kGap = 8;
const HoverCardBox kCard{0, 0, 260, 220};
// A 1000x800 screen; a sidebar 240 wide on its left edge, rows 36 high.
const HoverCardBox kScreen{0, 0, 1000, 800};

HoverCardBox Row(int y, int x = 0, int width = 240) {
  return HoverCardBox{x, y, width, 36};
}

TEST(HoverCardPlacementTest, SitsBesideTheRowOnTheSideAwayFromTheSidebarEdge) {
  const HoverCardBox row = Row(100);
  const HoverCardBox card = PlaceHoverCard(row, kCard, kScreen, /*prefer_right=*/true, kGap);
  EXPECT_EQ(card.x, row.right() + kGap);
  EXPECT_EQ(card.width, kCard.width);
  EXPECT_EQ(card.height, kCard.height);
}

TEST(HoverCardPlacementTest, StartsLevelWithTheTopOfTheRow) {
  const HoverCardBox card = PlaceHoverCard(Row(100), kCard, kScreen, true, kGap);
  EXPECT_EQ(card.y, 100);
}

TEST(HoverCardPlacementTest, TheBottomRowPushesTheCardUpToStayOnScreen) {
  const HoverCardBox row = Row(780 - 36);
  const HoverCardBox card = PlaceHoverCard(row, kCard, kScreen, true, kGap);
  EXPECT_EQ(card.bottom(), kScreen.bottom());
  EXPECT_FALSE(card.Intersects(row));
}

TEST(HoverCardPlacementTest, TheTopRowKeepsTheCardBelowTheScreenTop) {
  const HoverCardBox card =
      PlaceHoverCard(Row(-10), kCard, kScreen, true, kGap);
  EXPECT_EQ(card.y, kScreen.y);
}

TEST(HoverCardPlacementTest, ASidebarOnTheRightPutsTheCardOnTheLeft) {
  const HoverCardBox row = Row(100, /*x=*/760);
  const HoverCardBox card = PlaceHoverCard(row, kCard, kScreen, /*prefer_right=*/false, kGap);
  EXPECT_EQ(card.right(), row.x - kGap);
  EXPECT_FALSE(card.Intersects(row));
}

TEST(HoverCardPlacementTest, FlipsToTheOtherSideWhenThePreferredSideHasNoRoom) {
  // The row is at the right edge of the screen, so there is no room at its
  // right, whatever side the sidebar is thought to be on.
  const HoverCardBox screen{0, 0, 700, 800};
  const HoverCardBox row{460, 100, 240, 36};
  const HoverCardBox card = PlaceHoverCard(row, kCard, screen, true, kGap);
  EXPECT_EQ(card.right(), row.x - kGap);
  EXPECT_TRUE(screen.Contains(card));
}

TEST(HoverCardPlacementTest, NeverCoversTheRowWhateverTheScreenOrigin) {
  // A second monitor to the left of the primary one: negative coordinates.
  const HoverCardBox screen{-1920, 0, 1920, 1080};
  const HoverCardBox row{-1920, 1000, 240, 36};
  const HoverCardBox card = PlaceHoverCard(row, kCard, screen, true, kGap);
  EXPECT_TRUE(screen.Contains(card));
  EXPECT_FALSE(card.Intersects(row));
}

TEST(HoverCardPlacementTest, ACardWiderThanTheRoomOnEitherSideStaysOnScreenAnyway) {
  const HoverCardBox screen{0, 0, 300, 800};
  const HoverCardBox card = PlaceHoverCard(Row(100, 0, 200), kCard, screen, true, kGap);
  EXPECT_GE(card.x, screen.x);
  EXPECT_LE(card.right(), screen.right());
}

TEST(HoverCardPlacementTest, AWorkAreaThatIsNotAtTheOriginKeepsTheCardInsideIt) {
  // A second monitor to the right of the primary one, and a taskbar at the
  // top: the work area starts at (1920, 40).
  const HoverCardBox screen{1920, 40, 1920, 1000};
  const HoverCardBox row{1920, 980, 240, 36};
  const HoverCardBox card = PlaceHoverCard(row, kCard, screen, true, kGap);
  EXPECT_TRUE(screen.Contains(card));
  EXPECT_FALSE(card.Intersects(row));
  EXPECT_EQ(card.x, row.right() + kGap);
  EXPECT_EQ(card.bottom(), screen.bottom());
  const HoverCardBox top{1920, 40, 240, 36};
  EXPECT_EQ(PlaceHoverCard(top, kCard, screen, true, kGap).y, 40);
}

TEST(HoverCardPlacementTest, AMirroredWindowGetsTheCardOnTheLeftOfARightSidebar) {
  // A right-to-left UI puts the sidebar at the right of the window, whatever
  // the side setting says: the card goes to the page side, on its left.
  const HoverCardBox window{100, 100, 800, 600};
  const HoverCardBox row{660, 200, 240, 36};
  ASSERT_FALSE(HoverCardPrefersRight(row, window));
  const HoverCardBox card = PlaceHoverCard(
      row, kCard, kScreen, HoverCardPrefersRight(row, window), kGap);
  EXPECT_EQ(card.right(), row.x - kGap);
  EXPECT_FALSE(card.Intersects(row));
}

TEST(HoverCardPlacementTest, TheCardGoesOnThePageSideOfTheRow) {
  const HoverCardBox window{100, 100, 800, 600};
  EXPECT_TRUE(HoverCardPrefersRight(HoverCardBox{100, 200, 240, 36}, window));
  EXPECT_FALSE(HoverCardPrefersRight(HoverCardBox{660, 200, 240, 36}, window));
}

TEST(HoverCardTextTest, TheHostLosesALeadingWww) {
  EXPECT_EQ(HoverCardHost("https://www.example.com/a/b?q=1"), "example.com");
  EXPECT_EQ(HoverCardHost("https://docs.example.com/"), "docs.example.com");
}

TEST(HoverCardTextTest, APageWithoutAHostShowsItsScheme) {
  EXPECT_EQ(HoverCardHost("file:///home/me/a.html"), "file");
  EXPECT_EQ(HoverCardHost("about:blank"), "about");
  EXPECT_EQ(HoverCardHost(""), "");
}

TEST(HoverCardTextTest, TheFullUrlIsOnTheSecondLine) {
  const HoverCardText text =
      FormatHoverCardText(u"Example page", "https://www.example.com/a/b?q=1");
  EXPECT_EQ(text.title, u"Example page");
  EXPECT_EQ(text.host, u"example.com");
  EXPECT_EQ(text.url, u"https://www.example.com/a/b?q=1");
}

TEST(HoverCardTextTest, ATitleThatIsEmptyFallsBackToTheHost) {
  const HoverCardText text = FormatHoverCardText(u"", "https://example.com/");
  EXPECT_EQ(text.title, u"example.com");
}

TEST(HoverCardTextTest, TheDescriptionReadsHostThenUrl) {
  EXPECT_EQ(HoverCardDescription(FormatHoverCardText(
                u"T", "https://www.example.com/a")),
            u"example.com, https://www.example.com/a");
}

TEST(HoverCardTextTest, ABlankUrlGivesNoSecondLineAndNoDescription) {
  const HoverCardText text = FormatHoverCardText(u"New Tab", "");
  EXPECT_TRUE(text.url.empty());
  EXPECT_TRUE(HoverCardDescription(text).empty());
}

}  // namespace
}  // namespace aerie_sidebar
