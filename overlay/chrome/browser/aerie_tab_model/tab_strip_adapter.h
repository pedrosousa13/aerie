// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_AERIE_TAB_MODEL_TAB_STRIP_ADAPTER_H_
#define CHROME_BROWSER_AERIE_TAB_MODEL_TAB_STRIP_ADAPTER_H_

#include <map>
#include <string>

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

  const raw_ptr<Profile> profile_;
  const raw_ptr<TabModel> model_;
  const raw_ptr<const base::Clock> clock_;
  // Live tab handle to record id.
  std::map<tabs::TabHandle, std::string> ids_;
  base::ScopedObservation<GlobalBrowserCollection, BrowserCollectionObserver>
      browser_collection_observation_{this};
};

}  // namespace aerie_tab_model

#endif  // CHROME_BROWSER_AERIE_TAB_MODEL_TAB_STRIP_ADAPTER_H_
