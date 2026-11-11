// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_tab_model/sleep_schedule.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "base/check.h"

namespace aerie_tab_model {

SleepSchedule::SleepSchedule(const base::TickClock* clock, DueCallback on_due)
    : clock_(clock), on_due_(std::move(on_due)), timer_(clock) {}

SleepSchedule::~SleepSchedule() = default;

void SleepSchedule::SetDelay(std::optional<base::TimeDelta> delay) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  delay_ = delay;
  Rearm();
}

void SleepSchedule::SetDelayOverride(DelayOverride delay_override) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  delay_override_ = std::move(delay_override);
  Rearm();
}

void SleepSchedule::Touch(Key key) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  since_[key] = clock_->NowTicks();
  Rearm();
}

void SleepSchedule::Untrack(Key key) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (since_.erase(key)) {
    Rearm();
  }
}

std::optional<base::TimeDelta> SleepSchedule::next_fire_in() const {
  if (!timer_.IsRunning()) {
    return std::nullopt;
  }
  return timer_.desired_run_time() - clock_->NowTicks();
}

std::optional<base::TimeDelta> SleepSchedule::DelayFor(Key key) const {
  return delay_override_ ? delay_override_.Run(key, delay_) : delay_;
}

void SleepSchedule::Rearm() {
  if (running_) {
    return;
  }
  std::optional<base::TimeTicks> earliest;
  for (const auto& [key, since] : since_) {
    std::optional<base::TimeDelta> delay = DelayFor(key);
    if (!delay) {
      continue;
    }
    const base::TimeTicks due = since + *delay;
    if (!earliest || due < *earliest) {
      earliest = due;
    }
  }
  if (!earliest) {
    timer_.Stop();
    return;
  }
  const base::TimeDelta wait =
      std::max(*earliest - clock_->NowTicks(), base::TimeDelta());
  // Start() on a running timer replaces its task: still one pending task.
  timer_.Start(FROM_HERE, wait,
               base::BindOnce(&SleepSchedule::RunDueNow,
                              base::Unretained(this)));
}

void SleepSchedule::RunDueNow() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const base::TimeTicks now = clock_->NowTicks();
  std::vector<Key> due;
  for (const auto& [key, since] : since_) {
    std::optional<base::TimeDelta> delay = DelayFor(key);
    if (delay && since + *delay <= now) {
      due.push_back(key);
    }
  }
  running_ = true;
  for (Key key : due) {
    auto it = since_.find(key);
    if (it == since_.end()) {
      continue;  // A callback before this one untracked it.
    }
    // Forget it before the call: the call may Touch or Untrack other tabs.
    since_.erase(it);
    if (!on_due_.Run(key)) {
      since_[key] = clock_->NowTicks();
    }
  }
  running_ = false;
  Rearm();
}

}  // namespace aerie_tab_model
