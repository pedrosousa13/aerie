// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_sidebar/aerie_sidebar_hz_policy.h"

#include <numeric>

#include "testing/gtest/include/gtest/gtest.h"

namespace aerie_sidebar {
namespace {

// The numbers below are the design's (docs/sidebar-design.md, "Horizontal
// tabs"): tabs shrink from 180 to 36, icon-only below 72, the selected tab
// never below 120.

HzTabLayoutInput Input(int width, int count, int selected) {
  HzTabLayoutInput input;
  input.available_width = width;
  input.tab_count = count;
  input.selected_index = selected;
  return input;
}

TEST(AerieHzOverflowTest, TenTabsFitAtTheirShareAndShowTitles) {
  const HzTabLayoutResult r = ComputeHzTabLayout(Input(1200, 10, 3));
  ASSERT_EQ(r.widths.size(), 10u);
  for (int width : r.widths) {
    EXPECT_EQ(width, 120);
  }
  for (bool icon_only : r.icon_only) {
    EXPECT_FALSE(icon_only);
  }
  EXPECT_FALSE(r.scroll_needed);
}

TEST(AerieHzOverflowTest, FewTabsStopAtTheMaximum) {
  const HzTabLayoutResult r = ComputeHzTabLayout(Input(1200, 5, 0));
  ASSERT_EQ(r.widths.size(), 5u);
  for (int width : r.widths) {
    EXPECT_EQ(width, 180);
  }
  EXPECT_FALSE(r.scroll_needed);
}

TEST(AerieHzOverflowTest, CompactDensityHasASmallerMaximum) {
  HzTabLayoutInput input = Input(1200, 5, 0);
  input.density = HzDensity::kCompact;
  const HzTabLayoutResult r = ComputeHzTabLayout(input);
  for (int width : r.widths) {
    EXPECT_EQ(width, 160);
  }
}

TEST(AerieHzOverflowTest, FortyTabsGoIconOnlyAndTheSelectedOneKeeps120) {
  // (1920 - 120) / 39 = 46 for each of the others.
  const HzTabLayoutResult r = ComputeHzTabLayout(Input(1920, 40, 7));
  ASSERT_EQ(r.widths.size(), 40u);
  EXPECT_EQ(r.widths[7], 120);
  EXPECT_FALSE(r.icon_only[7]);
  for (size_t i = 0; i < r.widths.size(); ++i) {
    if (i == 7) {
      continue;
    }
    EXPECT_EQ(r.widths[i], 46) << i;
    EXPECT_TRUE(r.icon_only[i]) << i;
  }
  EXPECT_FALSE(r.scroll_needed);
}

TEST(AerieHzOverflowTest, FortyTabsInANarrowWindowShrinkToTheMinimumAndScroll) {
  const HzTabLayoutResult r = ComputeHzTabLayout(Input(1200, 40, 0));
  ASSERT_EQ(r.widths.size(), 40u);
  EXPECT_EQ(r.widths[0], 120);
  for (size_t i = 1; i < r.widths.size(); ++i) {
    EXPECT_EQ(r.widths[i], 36) << i;
    EXPECT_TRUE(r.icon_only[i]) << i;
  }
  // 39 * 36 + 120 = 1524 does not fit in 1200.
  EXPECT_TRUE(r.scroll_needed);
}

TEST(AerieHzOverflowTest, OneHundredTwentyTabsScrollAtTheMinimum) {
  const HzTabLayoutResult r = ComputeHzTabLayout(Input(1600, 120, 119));
  ASSERT_EQ(r.widths.size(), 120u);
  EXPECT_EQ(r.widths[119], 120);
  EXPECT_EQ(r.widths[0], 36);
  EXPECT_TRUE(r.icon_only[0]);
  EXPECT_TRUE(r.scroll_needed);
  EXPECT_EQ(std::accumulate(r.widths.begin(), r.widths.end(), 0),
            119 * 36 + 120);
}

TEST(AerieHzOverflowTest, TheIconOnlyLineIsBelow72) {
  // 7 tabs, selected first: others get (1000 - 120) / 6 = 146; use a width that
  // lands exactly on 72 and on 71 for the others.
  HzTabLayoutResult r = ComputeHzTabLayout(Input(120 + 72 * 4, 5, 0));
  EXPECT_EQ(r.widths[1], 72);
  EXPECT_FALSE(r.icon_only[1]);
  r = ComputeHzTabLayout(Input(120 + 71 * 4 + 3, 5, 0));
  EXPECT_EQ(r.widths[1], 71);
  EXPECT_TRUE(r.icon_only[1]);
}

TEST(AerieHzOverflowTest, NoTabsAndNoSelectionAreSafe) {
  EXPECT_TRUE(ComputeHzTabLayout(Input(1200, 0, -1)).widths.empty());
  const HzTabLayoutResult r = ComputeHzTabLayout(Input(1200, 3, -1));
  ASSERT_EQ(r.widths.size(), 3u);
  for (int width : r.widths) {
    EXPECT_EQ(width, 180);
  }
}

TEST(AerieHzMotionTest, OpenAndCloseTakeTheTokenDurations) {
  const HzTabMotion open = PlanHzTabMotion(HzTabEvent::kOpen, false);
  EXPECT_EQ(open.duration_ms, 200);
  EXPECT_EQ(open.width_duration_ms, 200);
  EXPECT_TRUE(open.animate_transform);
  EXPECT_TRUE(open.animate_opacity);
  const HzTabMotion close = PlanHzTabMotion(HzTabEvent::kClose, false);
  // The slide and fade take xfast; the width collapse takes tab-close.
  EXPECT_EQ(close.width_duration_ms, 140);
  EXPECT_LT(close.duration_ms, close.width_duration_ms);
  EXPECT_TRUE(close.animate_transform);
  EXPECT_EQ(close.slide_px, 8);
}

TEST(AerieHzMotionTest, ReducedMotionFadesOnlyAndIsShort) {
  const HzTabMotion open = PlanHzTabMotion(HzTabEvent::kOpen, true);
  EXPECT_EQ(open.duration_ms, 140);
  EXPECT_LE(open.width_duration_ms, 140);
  EXPECT_FALSE(open.animate_transform);
  EXPECT_TRUE(open.animate_opacity);
  const HzTabMotion close = PlanHzTabMotion(HzTabEvent::kClose, true);
  EXPECT_EQ(close.duration_ms, 90);
  EXPECT_LE(close.width_duration_ms, 90);
  EXPECT_FALSE(close.animate_transform);
  EXPECT_EQ(close.slide_px, 0);
}

// The sizes of the strips, from tokens.json (the literals are the design's:
// docs/sidebar-design.md, "Horizontal tabs").
TEST(AerieHzMetricsTest, OneRowComfortable) {
  const HzMetrics m = GetHzMetrics(HzRows::kOne, HzDensity::kComfortable);
  EXPECT_EQ(m.strip_height, 46);
  EXPECT_EQ(m.tab_height, 38);
  EXPECT_EQ(m.tab_strip_padding, 8);
  EXPECT_EQ(m.toolbar_height, 46);
  EXPECT_EQ(m.tab_max, 180);
  EXPECT_EQ(m.tile, 26);
  EXPECT_EQ(m.favicon, 18);
}

TEST(AerieHzMetricsTest, OneRowCompact) {
  const HzMetrics m = GetHzMetrics(HzRows::kOne, HzDensity::kCompact);
  EXPECT_EQ(m.strip_height, 38);
  EXPECT_EQ(m.tab_height, 30);
  EXPECT_EQ(m.tab_strip_padding, 8);
  EXPECT_EQ(m.tab_max, 160);
  EXPECT_EQ(m.tile, 24);
  EXPECT_EQ(m.favicon, 16);
}

TEST(AerieHzMetricsTest, TwoRowsComfortable) {
  const HzMetrics m = GetHzMetrics(HzRows::kTwo, HzDensity::kComfortable);
  EXPECT_EQ(m.strip_height, 42);
  EXPECT_EQ(m.tab_height, 34);
  EXPECT_EQ(m.tab_strip_padding, 8);
  EXPECT_EQ(m.toolbar_height, 40);
  EXPECT_EQ(m.tab_max, 180);
}

TEST(AerieHzMetricsTest, TwoRowsCompact) {
  const HzMetrics m = GetHzMetrics(HzRows::kTwo, HzDensity::kCompact);
  EXPECT_EQ(m.strip_height, 34);
  EXPECT_EQ(m.tab_height, 26);
  EXPECT_EQ(m.toolbar_height, 34);
  EXPECT_EQ(m.tile, 22);
}

TEST(AerieHzMetricsTest, TheSharedSizesDoNotDependOnTheRows) {
  for (HzRows rows : {HzRows::kOne, HzRows::kTwo}) {
    const HzMetrics m = GetHzMetrics(rows, HzDensity::kComfortable);
    EXPECT_EQ(m.address_pill, 240);
    EXPECT_EQ(m.address_pill_min, 160);
    EXPECT_EQ(m.flare_radius, 10);
    EXPECT_EQ(m.scroll_fade, 28);
  }
}

TEST(AerieHzMetricsTest, TheActiveMetricsAreSetAndCleared) {
  EXPECT_FALSE(GetActiveHzMetrics().has_value());
  SetActiveHzMetrics(GetHzMetrics(HzRows::kTwo, HzDensity::kCompact));
  ASSERT_TRUE(GetActiveHzMetrics().has_value());
  EXPECT_EQ(GetActiveHzMetrics()->tab_height, 26);
  SetActiveHzMetrics(std::nullopt);
  EXPECT_FALSE(GetActiveHzMetrics().has_value());
}

}  // namespace
}  // namespace aerie_sidebar
