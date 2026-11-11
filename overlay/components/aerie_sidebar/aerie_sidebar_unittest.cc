// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_design_tokens/aerie_tokens.h"
#include "components/aerie_sidebar/aerie_sidebar_layout.h"
#include "components/aerie_sidebar/aerie_sidebar_prefs.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/testing_pref_service.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace aerie_sidebar {
namespace {

// A profile with Aerie's prefs.
class AerieSidebarTest : public testing::Test {
 public:
  AerieSidebarTest() { RegisterProfilePrefs(prefs_.registry()); }

 protected:
  TestingPrefServiceSimple prefs_;
};

TEST_F(AerieSidebarTest, DefaultsAreTheSidebarShownAtTheTokenWidth) {
  EXPECT_EQ(prefs_.GetString(kLayoutPref), "sidebar");
  EXPECT_TRUE(prefs_.GetBoolean(kVisiblePref));
  EXPECT_EQ(prefs_.GetInteger(kWidthPref), aerie::tokens::kSizeSidebarWidth);
  EXPECT_EQ(GetWidth(prefs_), 260);
}

TEST_F(AerieSidebarTest, TheLayoutIsTheSidebarUnlessItSaysHorizontal) {
  EXPECT_TRUE(IsSidebarLayout(prefs_));
  prefs_.SetString(kLayoutPref, "classic");
  EXPECT_TRUE(IsSidebarLayout(prefs_));
  prefs_.SetString(kLayoutPref, "");
  EXPECT_TRUE(IsSidebarLayout(prefs_));
  prefs_.SetString(kLayoutPref, kLayoutHorizontal);
  EXPECT_FALSE(IsSidebarLayout(prefs_));
}

TEST_F(AerieSidebarTest, TheWidthIsClampedToTheTokenLimits) {
  EXPECT_EQ(ClampWidth(100), aerie::tokens::kSizeSidebarMin);
  EXPECT_EQ(ClampWidth(200), 200);
  EXPECT_EQ(ClampWidth(333), 333);
  EXPECT_EQ(ClampWidth(400), 400);
  EXPECT_EQ(ClampWidth(900), aerie::tokens::kSizeSidebarMax);
}

TEST_F(AerieSidebarTest, AStoredWidthIsClampedWhenRead) {
  prefs_.SetInteger(kWidthPref, 50);
  EXPECT_EQ(GetWidth(prefs_), 200);
  prefs_.SetInteger(kWidthPref, 5000);
  EXPECT_EQ(GetWidth(prefs_), 400);
  prefs_.SetInteger(kWidthPref, 310);
  EXPECT_EQ(GetWidth(prefs_), 310);
}

TEST_F(AerieSidebarTest, AZeroOrNegativeWidthIsTheDefault) {
  prefs_.SetInteger(kWidthPref, 0);
  EXPECT_EQ(GetWidth(prefs_), 260);
  prefs_.SetInteger(kWidthPref, -20);
  EXPECT_EQ(GetWidth(prefs_), 260);
}

TEST_F(AerieSidebarTest, ArrowKeysMoveTheWidthByTenPixelsWithinTheLimits) {
  EXPECT_EQ(ResizeByKey(260, /*grow=*/true), 270);
  EXPECT_EQ(ResizeByKey(260, /*grow=*/false), 250);
  EXPECT_EQ(ResizeByKey(395, /*grow=*/true), 400);
  EXPECT_EQ(ResizeByKey(400, /*grow=*/true), 400);
  EXPECT_EQ(ResizeByKey(205, /*grow=*/false), 200);
  EXPECT_EQ(ResizeByKey(200, /*grow=*/false), 200);
}

TEST_F(AerieSidebarTest, ToggleFlipsTheShownStateAndRemembersIt) {
  EXPECT_TRUE(IsVisible(prefs_));
  EXPECT_FALSE(ToggleVisible(&prefs_));
  EXPECT_FALSE(IsVisible(prefs_));
  EXPECT_FALSE(prefs_.GetBoolean(kVisiblePref));
  EXPECT_TRUE(ToggleVisible(&prefs_));
  EXPECT_TRUE(IsVisible(prefs_));
}

TEST_F(AerieSidebarTest, DraggingTheInnerEdgeResizesWithinTheLimits) {
  EXPECT_EQ(ResizeByDrag(260, 40, /*sidebar_on_right=*/false), 300);
  EXPECT_EQ(ResizeByDrag(260, -40, /*sidebar_on_right=*/false), 220);
  EXPECT_EQ(ResizeByDrag(260, 500, /*sidebar_on_right=*/false), 400);
  EXPECT_EQ(ResizeByDrag(260, -500, /*sidebar_on_right=*/false), 200);
  // A right-hand sidebar grows when the pointer moves left.
  EXPECT_EQ(ResizeByDrag(260, -40, /*sidebar_on_right=*/true), 300);
}

}  // namespace
}  // namespace aerie_sidebar
