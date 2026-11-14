// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_sidebar/aerie_sidebar_density.h"

#include "components/aerie_design_tokens/aerie_tokens.h"
#include "components/aerie_sidebar/aerie_sidebar_prefs.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/testing_pref_service.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace aerie_sidebar {
namespace {

namespace t = aerie::tokens;

class DensityTest : public testing::Test {
 public:
  DensityTest() { RegisterProfilePrefs(prefs_.registry()); }

 protected:
  TestingPrefServiceSimple prefs_;
};

TEST_F(DensityTest, TheDefaultsAreComfortableOnTheLeft) {
  EXPECT_EQ(prefs_.GetString(kDensityPref), "comfortable");
  EXPECT_EQ(GetDensity(prefs_), Density::kComfortable);
  EXPECT_EQ(prefs_.GetString(kSidePref), "left");
  EXPECT_FALSE(IsOnRight(prefs_));
}

TEST_F(DensityTest, EveryNameRoundTripsAndAnythingElseIsComfortable) {
  for (Density d : {Density::kComfortable, Density::kCompact, Density::kRail,
                    Density::kAutoHide}) {
    EXPECT_EQ(ParseDensity(DensityName(d)), d);
    prefs_.SetString(kDensityPref, DensityName(d));
    EXPECT_EQ(GetDensity(prefs_), d);
  }
  EXPECT_EQ(ParseDensity(""), Density::kComfortable);
  EXPECT_EQ(ParseDensity("huge"), Density::kComfortable);
}

TEST_F(DensityTest, ASideThatIsNotRightIsLeft) {
  prefs_.SetString(kSidePref, "right");
  EXPECT_TRUE(IsOnRight(prefs_));
  prefs_.SetString(kSidePref, "top");
  EXPECT_FALSE(IsOnRight(prefs_));
  prefs_.SetString(kSidePref, "");
  EXPECT_FALSE(IsOnRight(prefs_));
}

TEST_F(DensityTest, ComfortableSizesAreTheTokens) {
  const DensitySizes s = SizesFor(Density::kComfortable);
  EXPECT_EQ(s.row_height, 34);
  EXPECT_EQ(s.tile, 44);
  EXPECT_EQ(s.favicon, 20);
  EXPECT_EQ(s.inset, 8);
  EXPECT_FALSE(s.icons_only);
  EXPECT_EQ(s.fixed_width, 0);
  EXPECT_EQ(s.row_height, t::kSizeRowHeightComfortable);
}

TEST_F(DensityTest, CompactSizesAreTheTokens) {
  const DensitySizes s = SizesFor(Density::kCompact);
  EXPECT_EQ(s.row_height, 28);
  EXPECT_EQ(s.tile, 36);
  EXPECT_EQ(s.favicon, 18);
  EXPECT_EQ(s.inset, 4);
  EXPECT_LT(s.row_icon, SizesFor(Density::kComfortable).row_icon);
  EXPECT_FALSE(s.icons_only);
}

TEST_F(DensityTest, TheRailIsFiftySixWideWithSquareThirtyFourTiles) {
  const DensitySizes s = SizesFor(Density::kRail);
  EXPECT_EQ(s.fixed_width, 56);
  EXPECT_EQ(s.row_height, 34);
  EXPECT_EQ(s.tile, 34);
  EXPECT_TRUE(s.icons_only);
  // One column of tiles fits the rail exactly.
  EXPECT_EQ(s.inset * 2 + s.tile, s.fixed_width);
}

TEST_F(DensityTest, AutoHideLaysOutAsComfortableAndOnlyAutoHideHides) {
  const DensitySizes a = SizesFor(Density::kAutoHide);
  const DensitySizes c = SizesFor(Density::kComfortable);
  EXPECT_EQ(a.row_height, c.row_height);
  EXPECT_EQ(a.tile, c.tile);
  EXPECT_EQ(a.inset, c.inset);
  EXPECT_FALSE(a.icons_only);
  EXPECT_TRUE(IsAutoHide(Density::kAutoHide));
  EXPECT_FALSE(IsAutoHide(Density::kRail));
  EXPECT_FALSE(IsAutoHide(Density::kCompact));
}

TEST_F(DensityTest, AutoHiddenMeansAutoHideAndUndocked) {
  // The default is docked and comfortable.
  EXPECT_FALSE(IsAutoHidden(prefs_));
  prefs_.SetString(kDensityPref, kDensityAutoHide);
  // Docked auto-hide takes its room like any sidebar.
  EXPECT_TRUE(prefs_.GetBoolean(kVisiblePref));
  EXPECT_FALSE(IsAutoHidden(prefs_));
  prefs_.SetBoolean(kVisiblePref, false);
  EXPECT_TRUE(IsAutoHidden(prefs_));
  // A hidden sidebar of another density is not auto-hidden: it is hidden.
  prefs_.SetString(kDensityPref, kDensityCompact);
  EXPECT_FALSE(IsAutoHidden(prefs_));
}

TEST_F(DensityTest, TheEffectiveWidthIsTheRailsOrTheChosenOne) {
  EXPECT_EQ(EffectiveWidth(prefs_), 260);
  prefs_.SetInteger(kWidthPref, 320);
  EXPECT_EQ(EffectiveWidth(prefs_), 320);
  prefs_.SetString(kDensityPref, kDensityRail);
  EXPECT_EQ(EffectiveWidth(prefs_), 56);
  // The chosen width is kept for when the density changes back.
  EXPECT_EQ(prefs_.GetInteger(kWidthPref), 320);
  prefs_.SetString(kDensityPref, kDensityCompact);
  EXPECT_EQ(EffectiveWidth(prefs_), 320);
}

}  // namespace
}  // namespace aerie_sidebar
