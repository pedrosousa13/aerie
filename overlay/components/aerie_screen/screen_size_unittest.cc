// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_screen/screen_size.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace aerie_screen {
namespace {

TEST(ScreenSizeTest, ASmallWindowGetsTheSmallestBucket) {
  EXPECT_EQ(ScreenSizeForWindow(800, 600), (Size{1280, 800}));
  EXPECT_EQ(ScreenSizeForWindow(0, 0), (Size{1280, 800}));
  EXPECT_EQ(ScreenSizeForWindow(-5, -5), (Size{1280, 800}));
}

TEST(ScreenSizeTest, AWindowThatExactlyFillsABucketKeepsIt) {
  EXPECT_EQ(ScreenSizeForWindow(1280, 800), (Size{1280, 800}));
  EXPECT_EQ(ScreenSizeForWindow(1366, 768), (Size{1366, 768}));
  EXPECT_EQ(ScreenSizeForWindow(1920, 1080), (Size{1920, 1080}));
  EXPECT_EQ(ScreenSizeForWindow(3840, 2160), (Size{3840, 2160}));
}

TEST(ScreenSizeTest, OneExtraPixelMovesToTheNextBucket) {
  // 1366x768 is wider but shorter than the window, so it does not fit.
  EXPECT_EQ(ScreenSizeForWindow(1281, 800), (Size{1440, 900}));
  EXPECT_EQ(ScreenSizeForWindow(1280, 801), (Size{1440, 900}));
  EXPECT_EQ(ScreenSizeForWindow(1920, 1081), (Size{2560, 1440}));
}

TEST(ScreenSizeTest, BothDimensionsMustFit) {
  // A window 1366 wide and 800 tall does not fit 1366x768.
  EXPECT_EQ(ScreenSizeForWindow(1366, 768), (Size{1366, 768}));
  EXPECT_EQ(ScreenSizeForWindow(1366, 800), (Size{1440, 900}));
  // Narrow but tall.
  EXPECT_EQ(ScreenSizeForWindow(1000, 1000), (Size{1680, 1050}));
}

TEST(ScreenSizeTest, ARealisticLaptopWindow) {
  // A 1500x900 window on a 1920x1080 display.
  EXPECT_EQ(ScreenSizeForWindow(1500, 900), (Size{1680, 1050}));
}

TEST(ScreenSizeTest, AWindowLargerThanEveryBucketIsNeverClipped) {
  EXPECT_EQ(ScreenSizeForWindow(5120, 2880), (Size{5120, 2880}));
  EXPECT_EQ(ScreenSizeForWindow(3840, 2400), (Size{3840, 2400}));
  EXPECT_EQ(ScreenSizeForWindow(4000, 100), (Size{4000, 2160}));
}

TEST(ScreenSizeTest, TheScreenIsNeverSmallerThanTheWindow) {
  for (int w = 0; w <= 4200; w += 97) {
    for (int h = 0; h <= 2500; h += 89) {
      const Size screen = ScreenSizeForWindow(w, h);
      EXPECT_GE(screen.width, w);
      EXPECT_GE(screen.height, h);
    }
  }
}

TEST(EventScreenPointTest, IsTheClientPointPlusTheReportedChrome) {
  // A 1500x900 window with a 1500x800 page: events sit 100 below their client
  // point, as the page can work out from outerHeight - innerHeight.
  EXPECT_EQ(EventScreenPoint({50, 60}, {1500, 900}, {1500, 800}),
            (PointF{50, 160}));
}

TEST(EventScreenPointTest, DoesNotDependOnWhereTheRealWindowIs) {
  // The function has no window position to read, so it cannot leak one.
  EXPECT_EQ(EventScreenPoint({0, 0}, {1500, 900}, {1496, 834}),
            (PointF{4, 66}));
}

TEST(EventScreenPointTest, AddsTheChromeToAFractionalClientPoint) {
  // At a page zoom the client point has a fraction. The chrome is a whole
  // number of pixels, so screen - client is exactly outer - inner (4,66).
  const PointF screen =
      EventScreenPoint({10.25f, 20.75f}, {1500, 900}, {1496, 834});
  EXPECT_EQ(screen, (PointF{14.25f, 86.75f}));
  EXPECT_EQ(screen.x - 10.25f, 4.0f);
  EXPECT_EQ(screen.y - 20.75f, 66.0f);
}

TEST(EventScreenPointTest, KeepsFractionalPoints) {
  EXPECT_EQ(EventScreenPoint({10.5f, 20.25f}, {800, 600}, {800, 600}),
            (PointF{10.5f, 20.25f}));
}

TEST(ScreenPolicyTest, ProtectsOnlyWithBothFeaturesAndAToken) {
  EXPECT_TRUE(ShouldProtectScreen({true, true, true}));
  EXPECT_FALSE(ShouldProtectScreen({false, true, true}));
  EXPECT_FALSE(ShouldProtectScreen({true, false, true}));
}

TEST(ScreenPolicyTest, NoTokenMeansTheRealScreen) {
  // The browser sends no token when protection is off globally or for the
  // site: the real values come back.
  EXPECT_FALSE(ShouldProtectScreen({true, true, false}));
}

}  // namespace
}  // namespace aerie_screen
