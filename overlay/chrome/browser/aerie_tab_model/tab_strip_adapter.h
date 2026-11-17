// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_AERIE_TAB_MODEL_TAB_STRIP_ADAPTER_H_
#define CHROME_BROWSER_AERIE_TAB_MODEL_TAB_STRIP_ADAPTER_H_

#include <map>
#include <string>

#include "base/functional/callback.h"

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/time/clock.h"
#include "chrome/browser/ui/browser_window/public/browser_collection_observer.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "components/aerie_tab_model/tab_model.h"
#include "components/aerie_tab_model/tab_model_observer.h"
#include "components/tabs/public/tab_interface.h"

class BrowserWindowInterface;
class GlobalBrowserCollection;
class Profile;

namespace aerie_tab_model {

// Keeps the Tab Model in step with the tab strips of one profile (issue #176).
// It watches every normal browser window of the profile and, through
// TabStripModelObserver::OnTabStripModelChanged:
//  - adds a `today` TabRecord for each tab that is inserted,
//  - sets `position` when a tab moves, and `last_active` when it becomes the
//    active tab and when it stops being it,
//  - removes the record when the tab is closed.
// A tab that moves to another window is the same tab: its handle
// (tabs::TabHandle) stays, so its record stays. The handle is the join key at
// run time; the record id is what is stored.
//
// Pinned and Favorites tabs (issue #180) are kept differently. Their records
// outlive the tab: a record of the Pinned or Favorites section stays when its
// tab goes (a restart, a closed window, a page that closes itself), and the tab
// is created again, unloaded, by PinnedTabsController. Only a Today record goes
// with its tab, and the explicit close of a pinned tab moves its record to
// Today first. A tab's `position` follows the strip only in Today: Pinned and
// Favorites are ordered by the user, not by the strip.
//
// Today tabs and a restart (issue #223). The record id of every Today tab is
// saved with the tab in Chromium's session file (SaveRecordIdSoon). When session
// restore brings a tab back with that id (the id is on its WebContents when the
// tab is inserted, see session_record_id_hook.h), the tab re-attaches to the
// record the Tab Model read from state.json (TabModel::ClaimRestoredTab): same
// section, folder, position and manual name. A tab with no id, a malformed id
// or an id that matches nothing gets a fresh record. A tab that comes in before
// the Tab Model has read state.json gets a fresh record first and swaps it for
// the stored one when the read is merged. Nothing is matched by address, title
// or position.
//
// An archived record (issue #183) is the one exception to "a closed tab loses
// its record": the archiver writes `archived_at` before it closes the tab, and
// the adapter then keeps the record and forgets only the handle.
//
// It does not set the Chromium pinned bit, and it does not use TabGroupModel.
// It does not mirror the Tab Model order into the tab strip (issue #186).
class TabStripAdapter : public BrowserCollectionObserver,
                        public TabStripModelObserver,
                        public TabModelObserver {
 public:
  TabStripAdapter(Profile* profile, TabModel* model, const base::Clock* clock);
  TabStripAdapter(const TabStripAdapter&) = delete;
  TabStripAdapter& operator=(const TabStripAdapter&) = delete;
  ~TabStripAdapter() override;

  // The record id of a live tab, or an empty string.
  std::string RecordIdForTab(const tabs::TabInterface* tab) const;
  // The record id to save in the session for `tab`: its id when its record is a
  // Today record, else an empty string.
  std::string SessionRecordIdForTab(const tabs::TabInterface* tab) const;
  // The live tab of a record, or null when the record has no tab (a Pinned or
  // Favorites tab that is not restored yet).
  tabs::TabInterface* TabForRecord(const std::string& record_id) const;

  // The next tab that a watched window inserts is the tab of this record, not
  // a new Today tab. PinnedTabsController sets it around the call that creates
  // the unloaded tab.
  void SetPendingRestoreRecord(std::string record_id);

  // Runs when the tab of a Pinned or Favorites record goes away for a reason
  // that does not close the window (the record stays; the tab should come
  // back).
  void SetPinnedTabGoneCallback(base::RepeatingClosure callback);

  // Runs for every change of a tab that has a record: its record id and the tab
  // (issue #139: a new tab's address and title, for auto-filing). The callee
  // must return at once for a tab it does not care about.
  using TabChangedCallback =
      base::RepeatingCallback<void(const std::string& record_id,
                                   tabs::TabInterface* tab)>;
  void SetTabChangedCallback(TabChangedCallback callback);

  // Runs once for each tab that got a new Today record because it was inserted
  // (not for a tab that came from another window or a restored Pinned tab). The
  // record is in the model, and the Tab Model's observers have heard of it.
  using TabInsertedCallback =
      base::RepeatingCallback<void(const std::string& record_id,
                                   tabs::TabInterface* tab)>;
  void SetTabInsertedCallback(TabInsertedCallback callback);

  // Runs when a record stays in the model although its tab went away with a
  // closing window, because Chromium's session will bring the tab back (issue
  // #223). The record waits for a claim; the callee starts the restore pass
  // again, so that a window that opens later in the same run ends the wait.
  void SetRecordKeptCallback(base::RepeatingClosure callback);

 private:
  // BrowserCollectionObserver:
  void OnBrowserCreated(BrowserWindowInterface* browser) override;
  void OnBrowserClosed(BrowserWindowInterface* browser) override;

  // TabStripModelObserver:
  void OnTabStripModelChanged(TabStripModel* tab_strip_model,
                              const TabStripModelChange& change,
                              const TabStripSelectionChange& selection) override;

  // TabModelObserver:
  void OnModelLoaded() override;
  void OnTabMoved(const TabRecord& tab, const Placement& from) override;

  bool Watches(BrowserWindowInterface* browser) const;
  // True when the record of a tab that goes away with its closing window should
  // stay, because session restore will bring the tab back.
  bool KeepRecordWithClosingWindow(TabStripModel* tab_strip_model,
                                   const TabRecord& record) const;
  // Saves (or, for a record that left Today, clears) the record id of `tab` in
  // the session file. Runs after the call that is inserting the tab has
  // returned: the session service learns of the tab and its window from its own
  // observer of the same strip.
  void SaveRecordIdSoon(tabs::TabInterface* tab);
  void SaveRecordId(tabs::TabHandle handle);
  // Adds a record for `tab`, or, if the tab already has one (it came from
  // another window), moves the record.
  void TabInserted(tabs::TabInterface* tab, int index);
  void TouchActive(tabs::TabInterface* tab);
  // Sets `last_active` of the tab that stops being the selected one.
  void TouchLeft(tabs::TabInterface* tab);
  // TabStripModelObserver:
  void OnTabChangedAt(tabs::TabInterface* tab, TabChangeType change_type) override;

  const raw_ptr<Profile> profile_;
  const raw_ptr<TabModel> model_;
  const raw_ptr<const base::Clock> clock_;
  // Live tab handle to record id.
  std::map<tabs::TabHandle, std::string> ids_;
  std::string pending_restore_record_;
  // Tabs inserted before the Tab Model read state.json: the id each carried from
  // the session, to be claimed when the read is merged.
  std::map<tabs::TabHandle, std::string> deferred_claims_;
  base::RepeatingClosure pinned_tab_gone_;
  TabChangedCallback tab_changed_;
  TabInsertedCallback tab_inserted_;
  base::RepeatingClosure record_kept_;
  base::ScopedObservation<GlobalBrowserCollection, BrowserCollectionObserver>
      browser_collection_observation_{this};
  base::ScopedObservation<TabModel, TabModelObserver> model_observation_{this};
  base::WeakPtrFactory<TabStripAdapter> weak_factory_{this};
};

}  // namespace aerie_tab_model

#endif  // CHROME_BROWSER_AERIE_TAB_MODEL_TAB_STRIP_ADAPTER_H_
