// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_AERIE_TAB_MODEL_HORIZONTAL_ORDER_MIRROR_H_
#define CHROME_BROWSER_AERIE_TAB_MODEL_HORIZONTAL_ORDER_MIRROR_H_

#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "chrome/browser/ui/browser_window/public/browser_collection_observer.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "components/aerie_tab_model/tab_model_observer.h"
#include "components/prefs/pref_change_registrar.h"

class BrowserWindowInterface;
class GlobalBrowserCollection;
class Profile;
class TabStripModel;

namespace aerie_tab_model {

class TabModel;
class TabStripAdapter;

// Keeps the order of Chromium's tab strip in step with the Tab Model while the
// layout is horizontal (issue #186). The strip's order is what the user sees, so
// the tabs are moved into the order HorizontalStripOrder gives: Favorites,
// Pinned (folders flattened), then Today. The WebContents do not move and
// nothing is closed or reloaded: only TabStripModel::MoveWebContentsAt runs.
//
// A reorder in the strip is written back (PositionsFromStripOrder; Today
// already follows the strip through the TabStripAdapter), and what the strip
// cannot hold, such as a drag of a pinned tab into Today, is put back. In the
// sidebar layout the mirror does nothing, and the sidebar draws the Tab Model.
//
// TabStripModel cannot be changed from inside one of its own notifications, so
// the mirror posts a task and runs it after the notification returned. Tests
// call FlushForTesting().
class HorizontalOrderMirror : public BrowserCollectionObserver,
                              public TabStripModelObserver,
                              public TabModelObserver {
 public:
  HorizontalOrderMirror(Profile* profile,
                        TabModel* model,
                        TabStripAdapter* adapter);
  HorizontalOrderMirror(const HorizontalOrderMirror&) = delete;
  HorizontalOrderMirror& operator=(const HorizontalOrderMirror&) = delete;
  ~HorizontalOrderMirror() override;

  // True while aerie.sidebar.layout is one of the horizontal layouts.
  bool active() const;

  // Runs the mirror now if one is waiting.
  void FlushForTesting();

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
  void OnModelChanged() override;

  bool Watches(BrowserWindowInterface* browser) const;
  void ScheduleMirror();
  void MirrorAll();
  void MirrorStrip(TabStripModel* strip);
  // The record id of each tab of `strip`, in order (empty: no record).
  std::vector<std::string> StripIds(TabStripModel* strip) const;
  void WriteBack(TabStripModel* strip);

  const raw_ptr<Profile> profile_;
  const raw_ptr<TabModel> model_;
  const raw_ptr<TabStripAdapter> adapter_;
  PrefChangeRegistrar pref_registrar_;
  base::ScopedObservation<GlobalBrowserCollection, BrowserCollectionObserver>
      browser_collection_observation_{this};
  base::ScopedObservation<TabModel, TabModelObserver> model_observation_{this};
  bool mirror_pending_ = false;
  // True while the mirror moves tabs: those moves are not the user's.
  bool mirroring_ = false;
  base::WeakPtrFactory<HorizontalOrderMirror> weak_factory_{this};
};

}  // namespace aerie_tab_model

#endif  // CHROME_BROWSER_AERIE_TAB_MODEL_HORIZONTAL_ORDER_MIRROR_H_
