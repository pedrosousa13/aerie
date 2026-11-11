// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_AERIE_TAB_MODEL_SLEEP_CONTROLLER_H_
#define CHROME_BROWSER_AERIE_TAB_MODEL_SLEEP_CONTROLLER_H_

#include <stdint.h>

#include <map>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "base/time/tick_clock.h"
#include "chrome/browser/ui/browser_window/public/browser_collection_observer.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "components/aerie_tab_model/sleep_schedule.h"
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
                        public TabStripModelObserver {
 public:
  struct Eligibility {
    enum class State {
      kCanSleep,
      kAlreadyAsleep,
      kCannotSleep,
    };
    // Why a tab cannot sleep; the sidebar words it from the grd.
    enum class Cause {
      kNone,
      // The tab the user is viewing.
      kSelectedTab,
      // No page, or the performance manager is not there.
      kNotAvailable,
      // Chromium's discard policy says no; `reasons` has its words.
      kChromium,
    };
    State state = State::kCannotSleep;
    Cause cause = Cause::kNotAvailable;
    // Chromium's reasons, in its own (English) words, for kChromium.
    std::vector<std::string> reasons;
  };

  // `clock` must outlive the controller.
  SleepController(Profile* profile,
                  TabModelService* service,
                  const base::TickClock* clock);
  SleepController(const SleepController&) = delete;
  SleepController& operator=(const SleepController&) = delete;
  ~SleepController() override;

  Eligibility GetEligibility(tabs::TabInterface* tab) const;
  // Puts the tab to sleep at once ("Sleep now"). False when it cannot sleep.
  bool Sleep(tabs::TabInterface* tab);
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

 private:
  bool Watches(BrowserWindowInterface* browser) const;
  // Starts or stops the idle time of `tab`, from its state now.
  void Track(tabs::TabInterface* tab);
  // Called by the schedule: the tab has idled long enough.
  bool OnDue(SleepSchedule::Key key);
  void SetSleeping(tabs::TabInterface* tab, bool sleeping);
  void OnSleepAfterChanged();

  const raw_ptr<Profile> profile_;
  const raw_ptr<TabModelService> service_;
  const raw_ptr<const base::TickClock> clock_;
  SleepSchedule schedule_;
  PrefChangeRegistrar pref_registrar_;
  // The footprint Chromium estimated for a tab just before it slept, by handle.
  std::map<tabs::TabHandle, int64_t> pending_estimates_;
  std::map<tabs::TabHandle, int64_t> estimates_;
  base::ScopedObservation<GlobalBrowserCollection, BrowserCollectionObserver>
      browser_collection_observation_{this};
};

}  // namespace aerie_tab_model

#endif  // CHROME_BROWSER_AERIE_TAB_MODEL_SLEEP_CONTROLLER_H_
