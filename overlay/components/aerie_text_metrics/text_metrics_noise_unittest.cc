// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_text_metrics/text_metrics_noise.h"

#include <set>

#include "testing/gtest/include/gtest/gtest.h"

namespace aerie_text_metrics {
namespace {

TEST(TextMetricsNoiseTest, TheFactorStaysWithinTheRange) {
  EXPECT_DOUBLE_EQ(NoiseFactor(0), 1 - kMaxDeviation);
  EXPECT_DOUBLE_EQ(NoiseFactor(kSteps - 1), 1 + kMaxDeviation);
  for (int step = 0; step < kSteps; step += 997) {
    EXPECT_GE(NoiseFactor(step), 1 - kMaxDeviation);
    EXPECT_LE(NoiseFactor(step), 1 + kMaxDeviation);
  }
}

TEST(TextMetricsNoiseTest, TheMiddleStepsAreNearOne) {
  EXPECT_NEAR(NoiseFactor(kSteps / 2), 1.0, kMaxDeviation / 1000);
}

TEST(TextMetricsNoiseTest, ASameStepGivesTheSameFactor) {
  // A factor is a pure function of the step: nothing is random or counted, so
  // two loads of a site that hash to one step read one factor.
  EXPECT_EQ(NoiseFactor(12345), NoiseFactor(12345));
}

TEST(TextMetricsNoiseTest, NeighbouringStepsGiveDifferentFactors) {
  std::set<double> seen;
  for (int step = 5000; step < 5100; ++step) {
    seen.insert(NoiseFactor(step));
  }
  EXPECT_EQ(seen.size(), 100u);
}

TEST(TextMetricsNoiseTest, AStepOutsideTheRangeIsClamped) {
  EXPECT_EQ(NoiseFactor(-1), NoiseFactor(0));
  EXPECT_EQ(NoiseFactor(kSteps), NoiseFactor(kSteps - 1));
}

TEST(TextMetricsNoiseTest, OnlyAContextWithAllThreeConditionsIsNoised) {
  EXPECT_TRUE(ShouldNoiseTextMetrics({true, true, true}));
  EXPECT_FALSE(ShouldNoiseTextMetrics({false, true, true}));
  EXPECT_FALSE(ShouldNoiseTextMetrics({true, false, true}));
  // No token: protection is off for the site or the whole browser, and the
  // real metrics come back.
  EXPECT_FALSE(ShouldNoiseTextMetrics({true, true, false}));
}

}  // namespace
}  // namespace aerie_text_metrics
