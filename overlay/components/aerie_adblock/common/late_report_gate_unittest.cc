// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_adblock/common/late_report_gate.h"

#include <string>

#include "testing/gtest/include/gtest/gtest.h"

namespace aerie_adblock {
namespace {

base::TimeTicks Start() {
  return base::TimeTicks() + base::Seconds(10);
}

base::TimeDelta Ms(int ms) {
  return base::Milliseconds(ms);
}

TEST(LateReportGateTest, AllowsAReportAndThenWaits) {
  LateReportGate gate;
  EXPECT_TRUE(gate.Allow("a", "", Start()));
  EXPECT_FALSE(gate.Allow("b", "", Start() + Ms(kLateMinIntervalMs - 1)));
  EXPECT_TRUE(gate.Allow("b", "", Start() + Ms(kLateMinIntervalMs)));
}

TEST(LateReportGateTest, RefusesEmptyAndOversizedReports) {
  LateReportGate gate;
  const std::string too_big(kMaxLateTokenListBytes + 1, 'a');
  EXPECT_FALSE(gate.Allow("", "", Start()));
  EXPECT_FALSE(gate.Allow(too_big, "", Start()));
  EXPECT_FALSE(gate.Allow("", too_big, Start()));
  EXPECT_TRUE(gate.Allow(std::string(kMaxLateTokenListBytes, 'a'), "",
                         Start()));
}

TEST(LateReportGateTest, TheFrameGateIntervalIsHalfTheRenderersAndIsKept) {
  LateReportGate gate(Ms(kLateFrameGateIntervalMs));
  EXPECT_TRUE(gate.Allow("a", "", Start()));
  EXPECT_FALSE(gate.Allow("a", "", Start() + Ms(kLateFrameGateIntervalMs - 1)));
  EXPECT_TRUE(gate.Allow("a", "", Start() + Ms(kLateFrameGateIntervalMs)));
}

TEST(LateReportGateTest, RefusedReportsDoNotUseUpTheInterval) {
  LateReportGate gate;
  EXPECT_FALSE(gate.Allow("", "", Start()));
  EXPECT_TRUE(gate.Allow("a", "", Start()));
}

TEST(LateReportGateTest, StopsAfterTheCapPerDocument) {
  LateReportGate gate;
  base::TimeTicks t = Start();
  for (size_t i = 0; i < kMaxLateBatchesPerDocument; ++i) {
    ASSERT_TRUE(gate.Allow("a", "", t)) << i;
    t += Ms(kLateMinIntervalMs);
  }
  EXPECT_FALSE(gate.Allow("a", "", t));
  gate.Reset();
  EXPECT_TRUE(gate.Allow("a", "", t));
}

TEST(LateTokenBucketTest, AllowsABurstFarAboveHonestMultiFrameTraffic) {
  LateTokenBucket bucket(kLateTabBurst,
                         Ms(kLateTabRefillIntervalMs));
  // Many frames flushing at the same instant: all get through.
  for (size_t i = 0; i < kLateTabBurst; ++i) {
    ASSERT_TRUE(bucket.HasToken(Start())) << i;
    bucket.Take(Start());
  }
  EXPECT_FALSE(bucket.HasToken(Start()));
}

TEST(LateTokenBucketTest, RefillsOneTokenPerInterval) {
  LateTokenBucket bucket(kLateTabBurst, Ms(kLateTabRefillIntervalMs));
  for (size_t i = 0; i < kLateTabBurst; ++i) {
    bucket.Take(Start());
  }
  EXPECT_FALSE(bucket.HasToken(Start() + Ms(kLateTabRefillIntervalMs - 1)));
  EXPECT_TRUE(bucket.HasToken(Start() + Ms(kLateTabRefillIntervalMs)));
  bucket.Take(Start() + Ms(kLateTabRefillIntervalMs));
  EXPECT_FALSE(bucket.HasToken(Start() + Ms(kLateTabRefillIntervalMs)));
}

TEST(LateTokenBucketTest, NeverHoldsMoreThanTheCapacity) {
  LateTokenBucket bucket(3, Ms(kLateTabRefillIntervalMs));
  for (int i = 0; i < 3; ++i) {
    bucket.Take(Start() + Ms(10000));
  }
  EXPECT_FALSE(bucket.HasToken(Start() + Ms(10000)));
  EXPECT_TRUE(bucket.HasToken(Start() + Ms(10000 + kLateTabRefillIntervalMs)));
}

// A frame that reloads itself in a loop cannot use up the tab's share for good:
// after a pause the bucket is full again.
TEST(LateTokenBucketTest, ALoopingFrameOnlyDelaysOthersForTheMoment) {
  LateTokenBucket bucket(kLateTabBurst, Ms(kLateTabRefillIntervalMs));
  base::TimeTicks t = Start();
  for (int i = 0; i < 100000; ++i, t += Ms(1)) {
    if (bucket.HasToken(t)) {
      bucket.Take(t);
    }
  }
  t += Ms(kLateTabRefillIntervalMs * kLateTabBurst);
  for (size_t i = 0; i < kLateTabBurst; ++i) {
    ASSERT_TRUE(bucket.HasToken(t)) << i;
    bucket.Take(t);
  }
}

TEST(LateTokenBucketTest, CheckingDoesNotTakeAToken) {
  LateTokenBucket bucket(1, Ms(kLateTabRefillIntervalMs));
  EXPECT_TRUE(bucket.HasToken(Start()));
  EXPECT_TRUE(bucket.HasToken(Start()));
  bucket.Take(Start());
  EXPECT_FALSE(bucket.HasToken(Start()));
}

}  // namespace
}  // namespace aerie_adblock
