// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_AERIE_TAB_MODEL_SLEEP_CONTROLLER_H_
#define CHROME_BROWSER_AERIE_TAB_MODEL_SLEEP_CONTROLLER_H_

#include <stdint.h>

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/time/tick_clock.h"
#include "chrome/browser/ui/browser_window/public/browser_collection_observer.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "components/aerie_tab_model/sleep_schedule.h"
#include "components/aerie_tab_model/tab_model.h"
#include "components/aerie_tab_model/tab_model_observer.h"
#include "components/keyed_service/core/keyed_service.h"
#include "components/prefs/pref_change_registrar.h"
#include "components/tabs/public/tab_interface.h"

class BrowserWindowInterface;
class GlobalBrowserCollection;
class Profile;

namespace content {
class WebContents;
}  // namespace content

namespace aerie_tab_model {

class TabModelService;

// Puts inactive tabs to sleep (issue #179). Sleeping is Chromium's tab
// discarding (performance_manager::user_tuning::DiscardPage); this class is
// only the policy around it: when a tab is due, whether it may sleep, and the
// `sleeping` field of its Tab Model record.
//
// It is created lazily, after the window is interactive, so it costs nothing at
// startup. It watches every normal window of the profile like TabStripAdapter
// does, and keeps one SleepSchedule (one timer, no polling).
//
// A tab is tracked while it is in the background: it starts to idle when it
// stops being the selected tab, and the idle time starts again when the tab
// plays audio. The selected tab is never tracked. A tab that Chromium discards
// itself (memory pressure) is marked asleep too.
class SleepController : public KeyedService,
                        public BrowserCollectionObserver,
                        public TabStripModelObserver,
                        public TabModelObserver {
 public:
  struct Eligibility {
    enum class State {
      kCanSleep,
      kAlreadyAsleep,
      kCannotSleep,
    };
    State state = State::kCannotSleep;
    // The grd message (IDS_AERIE_SIDEBAR_SLEEP_TAB_*) that says why the Sleep
    // tab item is disabled: the tab is asleep, is selected, or one of
    // Chromium's reasons (audible, capture, developer tools, form edits, page
    // kind, background work) mapped to a sentence; a reason with no sentence of
    // its own gets the generic one. 0 when the tab can sleep.
    int tooltip_message_id = 0;
  };

  // `clock` must outlive the controller.
  SleepController(Profile* profile,
                  TabModelService* service,
                  const base::TickClock* clock);
  SleepController(const SleepController&) = delete;
  SleepController& operator=(const SleepController&) = delete;
  ~SleepController() override;

  // Who asks. The idle timer is automatic, so it is careful: it leaves a tab
  // that Chromium's proactive policy protects (notifications granted, title or
  // favicon changes in the background, recent sound, ...). The user's own
  // "Sleep now" refuses only what the brief lists: the selected tab, sound or
  // recent sound, developer tools, form edits and capture or a connected
  // device. A mail or chat tab can be put to sleep on request.
  enum class Request {
    kIdleTimer,
    kUser,
  };

  Eligibility GetEligibility(tabs::TabInterface* tab,
                             Request request = Request::kUser) const;
  // Puts the tab to sleep at once ("Sleep now" for kUser). False when it cannot
  // sleep.
  bool Sleep(tabs::TabInterface* tab, Request request = Request::kUser);
  // Wakes the tab by selecting it: Chromium reloads a discarded tab that is
  // selected, and nothing else loads it.
  void Wake(tabs::TabInterface* tab);

  // The memory that the sleeping tabs freed, as Chromium estimated it when they
  // went to sleep. A tab that Chromium discarded itself counts as 0.
  int64_t sleeping_memory_estimate_bytes() const;

  SleepSchedule* schedule() { return &schedule_; }

  // KeyedService:
  void Shutdown() override;

  // BrowserCollectionObserver:
  void OnBrowserCreated(BrowserWindowInterface* browser) override;
  void OnBrowserClosed(BrowserWindowInterface* browser) override;

  // TabStripModelObserver:
  void OnTabStripModelChanged(TabStripModel* tab_strip_model,
                              const TabStripModelChange& change,
                              const TabStripSelectionChange& selection) override;
  void OnTabChangedAt(tabs::TabInterface* tab,
                      TabChangeType change_type) override;

  // TabModelObserver. A folder's own sleep time is in its record (issue #141):
  // when a folder is edited or moved, or a tab moves to another folder, the
  // time of some tab can change, so the one timer is armed again.
  // A tab filed straight into a folder: the idle time may have started before
  // its record existed (the tab strip observers run in no fixed order), so the
  // timer is armed again to read the folder's time.
  void OnTabAdded(const TabRecord& record) override;
  void OnTabRemoved(const TabRecord& record) override { kept_ids_.erase(record.id); }
  void OnTabMoved(const TabRecord& tab, const Placement& from) override;
  // Keep loaded (issue #267) is a flag on the record: switching it changes the
  // time of that tab.
  void OnTabChanged(const TabRecord& record) override;
  void OnFolderAdded(const FolderRecord&) override { schedule_.Rearm(); }
  void OnFolderChanged(const FolderRecord&) override { schedule_.Rearm(); }
  void OnFolderMoved(const FolderRecord&, const Placement&) override {
    schedule_.Rearm();
  }
  void OnFolderRemoved(const FolderRecord&) override { schedule_.Rearm(); }

 private:
  bool Watches(BrowserWindowInterface* browser) const;
  // The Tab Model record says the tab is asleep (a restored tab that has not
  // been loaded, issue #180).
  bool RecordIsAsleep(tabs::TabInterface* tab) const;
  // The Tab Model record says the tab is kept loaded (issue #267).
  bool RecordIsKept(tabs::TabInterface* tab) const;
  // Makes Chromium's own discarding (memory pressure, Memory Saver) follow the
  // Tab Model flag of `record` (issue #267): a kept tab is not auto-discardable.
  // A kept tab that is asleep is loaded again. Does nothing for a record whose
  // tab is not in a window yet; the strip events call it again.
  void SyncKeepLoaded(const TabRecord& record, bool wake_on_switch);
  void SyncKeepLoaded(tabs::TabInterface* tab, bool wake_on_switch);
  // The ids of the records last seen kept: switching the flag on is the one
  // moment a sleeping tab is loaded again (a tab that is kept and then put to
  // sleep by the user stays asleep).
  std::set<std::string> kept_ids_;
  void LoadKeptTab(tabs::TabHandle handle);
  // Tries SyncKeepLoaded again later for a tab whose lifecycle unit is not
  // there yet.
  void ScheduleKeepRetry(tabs::TabHandle handle, int attempts_left);
  // The tabs (by handle) that Aerie made non-auto-discardable: the only ones it
  // makes auto-discardable again.
  std::set<int32_t> locked_tabs_;
  // Starts or stops the idle time of `tab`, from its state now.
  void Track(tabs::TabInterface* tab);
  // Called by the schedule: the tab has idled long enough.
  bool OnDue(SleepSchedule::Key key);
  void SetSleeping(tabs::TabInterface* tab, bool sleeping);
  void OnSleepAfterChanged();
  // The schedule's hook: the sleep time of the tab behind `key`, from the
  // folder chain of its record (Effective, issue #141), else `global`.
  std::optional<base::TimeDelta> DelayForTab(
      SleepSchedule::Key key,
      std::optional<base::TimeDelta> global) const;

  const raw_ptr<Profile> profile_;
  const raw_ptr<TabModelService> service_;
  SleepSchedule schedule_;
  PrefChangeRegistrar pref_registrar_;
  // The footprint Chromium estimated for a tab just before it slept, by handle.
  std::map<tabs::TabHandle, int64_t> pending_estimates_;
  std::map<tabs::TabHandle, int64_t> estimates_;
  base::ScopedObservation<GlobalBrowserCollection, BrowserCollectionObserver>
      browser_collection_observation_{this};
  base::ScopedObservation<TabModel, TabModelObserver> model_observation_{this};
  base::WeakPtrFactory<SleepController> weak_factory_{this};
};

}  // namespace aerie_tab_model

#endif  // CHROME_BROWSER_AERIE_TAB_MODEL_SLEEP_CONTROLLER_H_
