// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_AERIE_TAB_MODEL_TAB_ARCHIVER_H_
#define CHROME_BROWSER_AERIE_TAB_MODEL_TAB_ARCHIVER_H_

#include <map>
#include <memory>
#include <string>

#include "base/memory/raw_ptr.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "base/power_monitor/power_observer.h"
#include "base/scoped_observation.h"
#include "base/time/clock.h"
#include "base/time/tick_clock.h"
#include "chrome/browser/ui/browser_window/public/browser_collection_observer.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "components/aerie_tab_model/archive_schedule.h"
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

// How long after the archiver is built the tabs that it archives count as
// "catching up": they were overdue when the browser started. Motion stays off
// for them (the startup paths run with no animation).
inline constexpr base::TimeDelta kArchiveCatchUpWindow = base::Seconds(10);

// Archives unused Today tabs and restores them (issue #183, docs/sidebar-design.md,
// "Archive, as built"). The when is ArchiveSchedule (one timer, no poll); this
// class is the Chromium side: which tabs are exempt now, how a tab is closed
// and kept, and how a record becomes a tab again.
//
// Closing for archive. The record is written first (ArchiveTab), then the tab
// is closed with TabModel::Close(), which is CLOSE_NONE: no entry is made in
// Chromium's "Recently closed" list (TabRestoreService), so the archived tab
// exists in one place only, and restoring from the Archive cannot meet a
// second copy. TabStripAdapter keeps an archived record when its tab goes.
//
// It is created lazily, after the window is interactive (the sidebar asks for
// it a few seconds after it opens), so it costs nothing at startup. It watches
// every normal window of the profile for audio, as SleepController does.
class TabArchiver : public KeyedService,
                    public BrowserCollectionObserver,
                    public TabStripModelObserver,
                    public base::PowerSuspendObserver {
 public:
  class Observer : public base::CheckedObserver {
   public:
    // The tab of `record_id` is about to be archived. Its row is still in the
    // sidebar: this is the moment to take the copy that flies to the Archive
    // button.
    virtual void OnTabArchiving(const std::string& record_id) {}
    // The tab was archived and is closed. `animate` is false for the tabs that
    // were already overdue when the archiver started (a start after a long
    // time away): no motion on that path.
    virtual void OnTabArchived(const std::string& record_id, bool animate) {}
    // The archive did not happen after OnTabArchiving.
    virtual void OnTabArchiveFailed(const std::string& record_id) {}
  };

  // `clock` and `tick_clock` must outlive the archiver.
  TabArchiver(Profile* profile,
              TabModelService* service,
              const base::Clock* clock,
              const base::TickClock* tick_clock);
  TabArchiver(const TabArchiver&) = delete;
  TabArchiver& operator=(const TabArchiver&) = delete;
  ~TabArchiver() override;

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  // Archives `tab` now: the explicit path (the second press of a pinned close
  // after it moved to Today) and the timer's. False, and nothing changes, when
  // the tab has no Today record, is archived already, or its page asks to
  // confirm leaving (beforeunload): that one is closed by the normal path.
  bool Archive(tabs::TabInterface* tab);

  // Reopens the archived record `record_id` as a tab in `browser`, selected, at
  // the top of Today (or of its old folder, see RestoreArchivedTab). The tab
  // keeps its record, its Workspace and its last entries, so Back works.
  // Returns the new contents, or null for an unknown or unarchived record.
  content::WebContents* Restore(const std::string& record_id,
                                BrowserWindowInterface* browser);

  ArchiveSchedule* schedule() { return &schedule_; }

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

  // base::PowerSuspendObserver:
  void OnResume() override;

 private:
  bool Watches(BrowserWindowInterface* browser) const;
  // True when the schedule must leave the record alone now.
  bool IsExempt(const std::string& record_id) const;
  bool ArchiveWithMotion(tabs::TabInterface* tab, bool animate);
  // The schedule's call.
  bool OnDue(const std::string& record_id);
  void OnSettingsChanged();
  // Tells the schedule that a tab's audio state changed. A tab that stops
  // playing was in use until then: its timeout starts again.
  void NoteAudio(tabs::TabInterface* tab);

  const raw_ptr<Profile> profile_;
  const raw_ptr<TabModelService> service_;
  const raw_ptr<const base::Clock> clock_;
  ArchiveSchedule schedule_;
  PrefChangeRegistrar pref_registrar_;
  base::ObserverList<Observer> observers_;
  // Tabs that played audio at the last event, by handle.
  std::map<tabs::TabHandle, bool> audible_;
  // When the archiver was built. Tabs archived in the first moments after it
  // (kCatchUpWindow) were overdue at startup and are archived without motion.
  const base::TimeTicks created_;
  const raw_ptr<const base::TickClock> tick_clock_;
  base::ScopedObservation<GlobalBrowserCollection, BrowserCollectionObserver>
      browser_collection_observation_{this};
};

}  // namespace aerie_tab_model

#endif  // CHROME_BROWSER_AERIE_TAB_MODEL_TAB_ARCHIVER_H_
