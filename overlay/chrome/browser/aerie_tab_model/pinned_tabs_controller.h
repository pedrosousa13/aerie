// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_AERIE_TAB_MODEL_PINNED_TABS_CONTROLLER_H_
#define CHROME_BROWSER_AERIE_TAB_MODEL_PINNED_TABS_CONTROLLER_H_

#include <memory>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "chrome/browser/ui/browser_window/public/browser_collection_observer.h"
#include "components/aerie_tab_model/pinned_tabs.h"
#include "components/aerie_tab_model/tab_model_observer.h"
#include "components/tabs/public/tab_interface.h"

class BrowserWindowInterface;
class GlobalBrowserCollection;
class Profile;

namespace aerie_tab_model {

class TabModel;
class TabModelService;
class TabStripAdapter;

// The Pinned section and the Favorites grid of one profile (issue #180): what
// connects their records in the Tab Model to Chromium tabs.
//
//  - Restore. Aerie, not Chromium, brings Pinned and Favorites tabs back
//    (their tabs carry no Chromium pinned bit, so PinnedTabService has nothing
//    to restore). When the Tab Model is read, and when a window appears, every
//    record of those sections that has no tab gets one, created unloaded
//    (chrome::AddRestoredTab with kDeferUntilActivation): nothing loads, and
//    nothing is fetched, until the user selects the tab. Its row shows as asleep
//    (`sleeping` is set on the record) with the title that the tab last had.
//  - Back to pinned page: whether a pinned tab has left its pinned URL, and
//    the navigation back.
//  - Pinned close: Ctrl+W or the close button on a Pinned or Favorites tab
//    follows the `aerie.tabs.pinned_close` setting (HandleClose).
//  - The section commands of the context menu: pin, unpin, add to and remove
//    from Favorites.
class PinnedTabsController : public TabModelObserver,
                             public BrowserCollectionObserver {
 public:
  PinnedTabsController(Profile* profile,
                       TabModel* model,
                       TabStripAdapter* adapter);
  PinnedTabsController(const PinnedTabsController&) = delete;
  PinnedTabsController& operator=(const PinnedTabsController&) = delete;
  ~PinnedTabsController() override;

  // True when `tab` is the tab of a Pinned or Favorites record.
  bool IsPinnedOrFavorite(tabs::TabInterface* tab) const;

  // Creates the unloaded tabs of the records that have none, in the most
  // recently active normal window of the profile. Does nothing before the Tab
  // Model has been read or while there is no such window. Safe to call again.
  void RestoreMissingTabs();
  // The records that wait for a tab, for tests.
  std::vector<std::string> RecordsWithoutTab() const;

  // What a press of Ctrl+W or of the close button would do to `tab` now. Only
  // meaningful for a Pinned or Favorites tab; it names the tooltip.
  PinnedCloseAction ActionFor(tabs::TabInterface* tab) const;
  // Runs the press. Returns false, and does nothing, when `tab` is not a
  // Pinned or Favorites tab: the caller closes it as usual.
  bool HandleClose(tabs::TabInterface* tab);

  // True when the tab of a Pinned record has left its pinned page: the
  // committed URL's origin and path differ from `pinned_url` (the fragment and
  // the query are ignored).
  bool IsAwayFromPinnedPage(tabs::TabInterface* tab) const;
  // Selects the tab and navigates it to its pinned URL (a new history entry, so
  // Back returns to where it was).
  void GoBackToPinnedPage(tabs::TabInterface* tab);

  // The commands of the context menu. They use the tab's committed URL as the
  // pinned URL when they pin. AddToFavorites returns kFull, and changes
  // nothing, when the Workspace has 12 Favorites.
  bool Pin(tabs::TabInterface* tab);
  bool Unpin(tabs::TabInterface* tab);
  FavoriteResult AddToFavorites(tabs::TabInterface* tab);
  bool RemoveFromFavorites(tabs::TabInterface* tab);

  // TabModelObserver:
  void OnModelLoaded() override;

  // BrowserCollectionObserver:
  void OnBrowserCreated(BrowserWindowInterface* browser) override;

 private:
  class ResetThenSleep;

  const TabRecord* RecordFor(tabs::TabInterface* tab) const;
  GURL CommittedUrl(tabs::TabInterface* tab) const;
  PinnedCloseMode Mode() const;
  void ScheduleRestore();
  BrowserWindowInterface* RestoreTarget() const;
  // Selects the tab that follows `tab` in the sidebar, else the one above it,
  // else opens a new tab; only for the selected tab.
  void SwitchAwayFrom(tabs::TabInterface* tab);
  void SleepTab(tabs::TabInterface* tab);
  void FinishReset(ResetThenSleep* reset);

  const raw_ptr<Profile> profile_;
  const raw_ptr<TabModel> model_;
  const raw_ptr<TabStripAdapter> adapter_;
  bool loaded_ = false;
  bool restore_scheduled_ = false;
  std::vector<std::unique_ptr<ResetThenSleep>> resets_;
  base::ScopedObservation<TabModel, TabModelObserver> model_observation_{this};
  base::ScopedObservation<GlobalBrowserCollection, BrowserCollectionObserver>
      browser_collection_observation_{this};
  base::WeakPtrFactory<PinnedTabsController> weak_factory_{this};
};

}  // namespace aerie_tab_model

#endif  // CHROME_BROWSER_AERIE_TAB_MODEL_PINNED_TABS_CONTROLLER_H_
