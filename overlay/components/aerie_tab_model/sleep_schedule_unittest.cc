// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_tab_model/sleep_schedule.h"

#include <vector>

#include "base/functional/bind.h"
#include "base/test/task_environment.h"
#include "base/time/time.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace aerie_tab_model {

namespace {

class SleepScheduleTest : public testing::Test {
 protected:
  SleepScheduleTest()
      : schedule_(task_environment_.GetMockTickClock(),
                  base::BindRepeating(&SleepScheduleTest::OnDue,
                                      base::Unretained(this))) {
    schedule_.SetDelay(base::Minutes(15));
  }

  bool OnDue(SleepSchedule::Key key) {
    due_.push_back(key);
    return handled_;
  }

  base::test::SingleThreadTaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  SleepSchedule schedule_;
  std::vector<SleepSchedule::Key> due_;
  bool handled_ = true;
};

TEST_F(SleepScheduleTest, ManyTabsShareOneTimer) {
  for (int key = 1; key <= 50; ++key) {
    schedule_.Touch(key);
    task_environment_.FastForwardBy(base::Seconds(1));
  }
  EXPECT_EQ(task_environment_.GetPendingMainThreadTaskCount(), 1u);
  EXPECT_TRUE(schedule_.timer_pending());
}

TEST_F(SleepScheduleTest, ATabDueAfterTheDelayIsReportedOnceAndForgotten) {
  schedule_.Touch(7);
  task_environment_.FastForwardBy(base::Minutes(14));
  EXPECT_TRUE(due_.empty());
  task_environment_.FastForwardBy(base::Minutes(1));
  EXPECT_EQ(due_, std::vector<SleepSchedule::Key>({7}));
  EXPECT_FALSE(schedule_.IsTracked(7));
  EXPECT_FALSE(schedule_.timer_pending());
}

TEST_F(SleepScheduleTest, TheTimerIsArmedForTheEarliestTabAndThenTheNext) {
  schedule_.Touch(1);
  task_environment_.FastForwardBy(base::Minutes(5));
  schedule_.Touch(2);
  EXPECT_EQ(schedule_.next_fire_in(), base::Minutes(10));
  task_environment_.FastForwardBy(base::Minutes(10));
  EXPECT_EQ(due_, std::vector<SleepSchedule::Key>({1}));
  EXPECT_EQ(schedule_.next_fire_in(), base::Minutes(5));
  task_environment_.FastForwardBy(base::Minutes(5));
  EXPECT_EQ(due_, std::vector<SleepSchedule::Key>({1, 2}));
}

TEST_F(SleepScheduleTest, TouchRestartsTheIdleTime) {
  schedule_.Touch(1);
  task_environment_.FastForwardBy(base::Minutes(10));
  schedule_.Touch(1);
  task_environment_.FastForwardBy(base::Minutes(10));
  EXPECT_TRUE(due_.empty());
  task_environment_.FastForwardBy(base::Minutes(5));
  EXPECT_EQ(due_.size(), 1u);
}

TEST_F(SleepScheduleTest, UntrackCancelsAndTheLastOneStopsTheTimer) {
  schedule_.Touch(1);
  schedule_.Touch(2);
  schedule_.Untrack(1);
  EXPECT_TRUE(schedule_.timer_pending());
  schedule_.Untrack(2);
  EXPECT_FALSE(schedule_.timer_pending());
  EXPECT_EQ(task_environment_.GetPendingMainThreadTaskCount(), 0u);
  task_environment_.FastForwardBy(base::Hours(1));
  EXPECT_TRUE(due_.empty());
}

TEST_F(SleepScheduleTest, NeverStopsEverythingAndRearmingNeedsNoRestart) {
  schedule_.Touch(1);
  schedule_.SetDelay(std::nullopt);
  EXPECT_FALSE(schedule_.timer_pending());
  task_environment_.FastForwardBy(base::Hours(10));
  EXPECT_TRUE(due_.empty());
  // A tab that is tracked while the delay is "never" is kept and counts from
  // when it was touched.
  schedule_.SetDelay(base::Minutes(30));
  EXPECT_EQ(due_, std::vector<SleepSchedule::Key>());
  task_environment_.FastForwardBy(base::Minutes(1));
  EXPECT_EQ(due_.size(), 1u);
}

TEST_F(SleepScheduleTest, ChangingTheDelayRearmsTheTimer) {
  schedule_.SetDelay(base::Hours(1));
  schedule_.Touch(1);
  EXPECT_EQ(schedule_.next_fire_in(), base::Hours(1));
  schedule_.SetDelay(base::Minutes(15));
  EXPECT_EQ(schedule_.next_fire_in(), base::Minutes(15));
  EXPECT_EQ(task_environment_.GetPendingMainThreadTaskCount(), 1u);
}

TEST_F(SleepScheduleTest, ATabThatCannotSleepIsTriedAgainAfterAnotherDelay) {
  handled_ = false;
  schedule_.Touch(1);
  task_environment_.FastForwardBy(base::Minutes(15));
  EXPECT_EQ(due_.size(), 1u);
  EXPECT_TRUE(schedule_.IsTracked(1));
  EXPECT_EQ(schedule_.next_fire_in(), base::Minutes(15));
  handled_ = true;
  task_environment_.FastForwardBy(base::Minutes(15));
  EXPECT_EQ(due_.size(), 2u);
  EXPECT_FALSE(schedule_.IsTracked(1));
}

TEST_F(SleepScheduleTest, APerTabHookCanOverrideTheDelay) {
  schedule_.SetDelayOverride(base::BindRepeating(
      [](SleepSchedule::Key key, std::optional<base::TimeDelta> global)
          -> std::optional<base::TimeDelta> {
        if (key == 2) {
          return base::Minutes(5);
        }
        if (key == 3) {
          return std::nullopt;  // Never, for this tab.
        }
        return global;
      }));
  schedule_.Touch(1);
  schedule_.Touch(2);
  schedule_.Touch(3);
  task_environment_.FastForwardBy(base::Minutes(5));
  EXPECT_EQ(due_, std::vector<SleepSchedule::Key>({2}));
  task_environment_.FastForwardBy(base::Minutes(10));
  EXPECT_EQ(due_, std::vector<SleepSchedule::Key>({2, 1}));
  EXPECT_TRUE(schedule_.IsTracked(3));
  EXPECT_FALSE(schedule_.timer_pending());
}

TEST_F(SleepScheduleTest, RunDueNowReadsTheClockForTestsThatMoveIt) {
  schedule_.Touch(1);
  schedule_.RunDueNow();
  EXPECT_TRUE(due_.empty());
}

}  // namespace

}  // namespace aerie_tab_model
