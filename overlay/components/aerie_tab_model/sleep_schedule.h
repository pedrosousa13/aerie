// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_TAB_MODEL_SLEEP_SCHEDULE_H_
#define COMPONENTS_AERIE_TAB_MODEL_SLEEP_SCHEDULE_H_

#include <stdint.h>

#include <map>
#include <optional>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/sequence_checker.h"
#include "base/time/tick_clock.h"
#include "base/time/time.h"
#include "base/timer/timer.h"

namespace aerie_tab_model {

// When each idle tab is due to sleep (issue #179). Headless: a key is any
// number that names a tab (the browser uses the raw value of the tab handle),
// and time comes from the injected clock.
//
// It owns exactly one base::OneShotTimer, armed for the earliest tab to expire.
// There is no poll loop and no timer per tab: a change (Touch, Untrack, a new
// delay) arms the one timer again, and the timer, when it runs, reports every
// tab that is due and arms itself for the next one. With no tab to expire, or
// with the delay "never", no timer is pending.
class SleepSchedule {
 public:
  using Key = int32_t;
  // A tab is due. Returns true when the tab went to sleep (it is then
  // forgotten). Returns false when it cannot sleep now: it is tried again
  // after another delay.
  using DueCallback = base::RepeatingCallback<bool(Key)>;
  // The per-tab hook (issue #141, per-folder sleep time). It gets the global
  // delay and returns the delay for this tab; nullopt means never.
  using DelayOverride = base::RepeatingCallback<std::optional<base::TimeDelta>(
      Key,
      std::optional<base::TimeDelta> global)>;

  // `clock` must outlive the schedule; it also drives the timer.
  SleepSchedule(const base::TickClock* clock, DueCallback on_due);
  SleepSchedule(const SleepSchedule&) = delete;
  SleepSchedule& operator=(const SleepSchedule&) = delete;
  ~SleepSchedule();

  // The global delay; nullopt means never. Arms the timer again at once, so a
  // change of the setting needs no restart.
  void SetDelay(std::optional<base::TimeDelta> delay);
  void SetDelayOverride(DelayOverride delay_override);

  // The tab has been idle since now: starts, or restarts, its idle time.
  void Touch(Key key);
  // The tab cannot sleep now (it is the selected tab, or plays audio).
  void Untrack(Key key);
  bool IsTracked(Key key) const { return since_.contains(key); }
  size_t tracked_count() const { return since_.size(); }

  bool timer_pending() const { return timer_.IsRunning(); }
  // How long until the timer runs, or nullopt when it is not armed.
  std::optional<base::TimeDelta> next_fire_in() const;

  // Reports every tab that is due by the clock, then arms the timer again. The
  // timer calls it; a test that moves its own clock calls it too.
  void RunDueNow();

 private:
  std::optional<base::TimeDelta> DelayFor(Key key) const;
  // Arms the timer for the earliest tab to expire, or stops it.
  void Rearm();

  const raw_ptr<const base::TickClock> clock_;
  const DueCallback on_due_;
  DelayOverride delay_override_;
  std::optional<base::TimeDelta> delay_;
  std::map<Key, base::TimeTicks> since_;
  base::OneShotTimer timer_;
  // True while RunDueNow calls out: changes made by the callback arm the timer
  // once at the end.
  bool running_ = false;
  SEQUENCE_CHECKER(sequence_checker_);
};

}  // namespace aerie_tab_model

#endif  // COMPONENTS_AERIE_TAB_MODEL_SLEEP_SCHEDULE_H_
