// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_tab_model/sleep_summary.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace aerie_tab_model {

namespace {
constexpr int64_t kMb = 1024 * 1024;
}

TEST(SleepSummaryTest, TheEstimateIsRoundedToTenMegabytes) {
  EXPECT_EQ(RoundMemoryEstimateMb(184 * kMb), 180);
  EXPECT_EQ(RoundMemoryEstimateMb(185 * kMb), 190);
  EXPECT_EQ(RoundMemoryEstimateMb(10 * kMb), 10);
  EXPECT_EQ(RoundMemoryEstimateMb(1000 * kMb), 1000);
}

TEST(SleepSummaryTest, LessThanFiveMegabytesIsNoEstimate) {
  EXPECT_EQ(RoundMemoryEstimateMb(0), 0);
  EXPECT_EQ(RoundMemoryEstimateMb(4 * kMb), 0);
  EXPECT_EQ(RoundMemoryEstimateMb(-5), 0);
  EXPECT_EQ(RoundMemoryEstimateMb(5 * kMb), 10);
}

}  // namespace aerie_tab_model
