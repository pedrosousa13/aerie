// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_AERIE_TAB_MODEL_TAB_STRIP_ADAPTER_H_
#define CHROME_BROWSER_AERIE_TAB_MODEL_TAB_STRIP_ADAPTER_H_

#include <map>
#include <string>

#include "base/functional/callback.h"

#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "base/time/clock.h"
#include "chrome/browser/ui/browser_window/public/browser_collection_observer.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "components/tabs/public/tab_interface.h"

class BrowserWindowInterface;
class GlobalBrowserCollection;
class Profile;

namespace aerie_tab_model {

class TabModel;

// Keeps the Tab Model in step with the tab strips of one profile (issue #176).
// It watches every normal browser window of the profile and, through
// TabStripModelObserver::OnTabStripModelChanged:
//  - adds a `today` TabRecord for each tab that is inserted,
//  - sets `position` when a tab moves, and `last_active` when it becomes the
//    active tab,
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
// It does not set the Chromium pinned bit, and it does not use TabGroupModel.
// It does not mirror the Tab Model order into the tab strip (issue #186).
class TabStripAdapter : public BrowserCollectionObserver,
                        public TabStripModelObserver {
 public:
  TabStripAdapter(Profile* profile, TabModel* model, const base::Clock* clock);
  TabStripAdapter(const TabStripAdapter&) = delete;
  TabStripAdapter& operator=(const TabStripAdapter&) = delete;
  ~TabStripAdapter() override;

  // The record id of a live tab, or an empty string.
  std::string RecordIdForTab(const tabs::TabInterface* tab) const;
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

 private:
  // BrowserCollectionObserver:
  void OnBrowserCreated(BrowserWindowInterface* browser) override;
  void OnBrowserClosed(BrowserWindowInterface* browser) override;

  // TabStripModelObserver:
  void OnTabStripModelChanged(TabStripModel* tab_strip_model,
                              const TabStripModelChange& change,
                              const TabStripSelectionChange& selection) override;

  bool Watches(BrowserWindowInterface* browser) const;
  // Adds a record for `tab`, or, if the tab already has one (it came from
  // another window), moves the record.
  void TabInserted(tabs::TabInterface* tab, int index);
  void TouchActive(tabs::TabInterface* tab);
  // TabStripModelObserver:
  void OnTabChangedAt(tabs::TabInterface* tab, TabChangeType change_type) override;

  const raw_ptr<Profile> profile_;
  const raw_ptr<TabModel> model_;
  const raw_ptr<const base::Clock> clock_;
  // Live tab handle to record id.
  std::map<tabs::TabHandle, std::string> ids_;
  std::string pending_restore_record_;
  base::RepeatingClosure pinned_tab_gone_;
  base::ScopedObservation<GlobalBrowserCollection, BrowserCollectionObserver>
      browser_collection_observation_{this};
};

}  // namespace aerie_tab_model

#endif  // CHROME_BROWSER_AERIE_TAB_MODEL_TAB_STRIP_ADAPTER_H_
