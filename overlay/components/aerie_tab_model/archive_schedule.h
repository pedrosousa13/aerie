// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_TAB_MODEL_ARCHIVE_SCHEDULE_H_
#define COMPONENTS_AERIE_TAB_MODEL_ARCHIVE_SCHEDULE_H_

#include <map>
#include <optional>
#include <string>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "base/sequence_checker.h"
#include "base/time/clock.h"
#include "base/time/tick_clock.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "components/aerie_tab_model/archive.h"
#include "components/aerie_tab_model/tab_model.h"
#include "components/aerie_tab_model/tab_model_observer.h"

namespace aerie_tab_model {

// The timeout that applies to `tab` (issue #183): the global archive delay now.
// Folder behaviour (issue #141, slice L) will override it per folder here, and
// the archiver needs no other change. nullopt means never.
std::optional<base::TimeDelta> EffectiveArchiveTimeout(
    const TabModel& model,
    const TabRecord& tab,
    std::optional<base::TimeDelta> global);

// When the unused Today tabs are archived, and when old archived records are
// dropped (issue #183). Headless: the tab itself is closed by the owner.
//
// It owns exactly one base::OneShotTimer, armed for the earliest of: a
// candidate's `last_active` plus its timeout, and the oldest archived record
// plus the retention. There is no poll loop and no timer per tab. Anything that
// can move that moment arms it again: every change of the Tab Model (which is
// how a tab that is activated or used shows up, through `last_active`), a new
// delay (SetArchiveAfter, SetKeep), and Rearm(), which the owner calls when an
// exemption starts or ends (a tab is selected or plays audio) and when the
// computer wakes from sleep (the timer runs on the monotonic clock, which does
// not count the time asleep, so the wall clock decides again).
//
// A candidate is a Today tab that is not archived, not waiting for session
// restore, and that `exempt` does not name. Pinned, Favorites and the tabs of
// Pinned folders are in other sections, so they are never candidates. The
// tab's last use is the record's `last_active` (wall clock), so a tab that was
// idle before a restart is archived on time.
class ArchiveSchedule : public TabModelObserver {
 public:
  // Archives the tab of this record. True when it was archived (the record has
  // `archived_at` then). False when it could not be closed now; the tab is
  // tried again after another full delay.
  using ArchiveCallback = base::RepeatingCallback<bool(const std::string&)>;
  // True when the tab must not be archived now: it is the selected tab, plays
  // audio, or has no live tab.
  using ExemptCallback = base::RepeatingCallback<bool(const std::string&)>;

  // `model`, `clock` and `tick_clock` must outlive the schedule. `clock` is the
  // wall clock the records use; `tick_clock` drives the timer. Neither starts
  // anything: the first Rearm does (the owner calls it once the window is
  // interactive).
  ArchiveSchedule(TabModel* model,
                  const base::Clock* clock,
                  const base::TickClock* tick_clock,
                  ArchiveCallback archive,
                  ExemptCallback exempt);
  ArchiveSchedule(const ArchiveSchedule&) = delete;
  ArchiveSchedule& operator=(const ArchiveSchedule&) = delete;
  ~ArchiveSchedule() override;

  // nullopt means never. Arms the timer again at once, so a change of the
  // setting needs no restart.
  void SetArchiveAfter(std::optional<base::TimeDelta> delay);
  // How long archived records stay; nullopt means for ever.
  void SetKeep(std::optional<base::TimeDelta> keep);

  // Arms the timer for the earliest moment, or stops it.
  void Rearm();
  // Drops the archived records that are too old and archives every tab that is
  // due by the clock, then arms the timer again. The timer calls it; a test
  // that moves its own clock calls it too.
  void RunDueNow();

  bool timer_pending() const { return timer_.IsRunning(); }
  // How long until the timer runs, or nullopt when it is not armed.
  std::optional<base::TimeDelta> next_fire_in() const;

  // TabModelObserver:
  void OnTabAdded(const TabRecord&) override { Rearm(); }
  void OnTabChanged(const TabRecord&) override { Rearm(); }
  void OnTabMoved(const TabRecord&, const Placement&) override { Rearm(); }
  void OnTabRemoved(const TabRecord&) override { Rearm(); }
  void OnModelLoaded() override { Rearm(); }

 private:
  // The moment `tab` is due, or nullopt when it is not a candidate.
  std::optional<base::Time> DueTime(const TabRecord& tab) const;

  const raw_ptr<TabModel> model_;
  const raw_ptr<const base::Clock> clock_;
  const raw_ptr<const base::TickClock> tick_clock_;
  const ArchiveCallback archive_;
  const ExemptCallback exempt_;
  std::optional<base::TimeDelta> archive_after_;
  std::optional<base::TimeDelta> keep_ = kDefaultArchiveKeep;
  // A tab that could not be archived is counted as used at that moment.
  std::map<std::string, base::Time> retry_from_;
  base::OneShotTimer timer_;
  // True while RunDueNow calls out: the changes it makes arm the timer once, at
  // the end.
  bool running_ = false;
  base::ScopedObservation<TabModel, TabModelObserver> model_observation_{this};
  SEQUENCE_CHECKER(sequence_checker_);
};

}  // namespace aerie_tab_model

#endif  // COMPONENTS_AERIE_TAB_MODEL_ARCHIVE_SCHEDULE_H_
