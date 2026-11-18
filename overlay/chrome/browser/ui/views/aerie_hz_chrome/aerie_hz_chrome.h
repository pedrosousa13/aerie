// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_AERIE_HZ_CHROME_AERIE_HZ_CHROME_H_
#define CHROME_BROWSER_UI_VIEWS_AERIE_HZ_CHROME_AERIE_HZ_CHROME_H_

#include <string>

#include "base/memory/raw_ptr.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "components/aerie_tab_model/tab_model_observer.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"

class BrowserView;
class Profile;
class TabStripRegionView;
class ToolbarView;

namespace aerie_tab_model {
class TabModel;
}

// The parts that Aerie adds to Chromium's horizontal layouts (issue #186).
//
// The Workspace chip is a label at the start of the tab strip's row: the
// Workspace's name on its colour. It is read-only here (the switcher is the
// Workspaces slice); a screen reader says "Workspace: <name>".
class AerieHzWorkspaceChip : public views::View,
                             public aerie_tab_model::TabModelObserver {
  METADATA_HEADER(AerieHzWorkspaceChip, views::View)

 public:
  explicit AerieHzWorkspaceChip(Profile* profile);
  AerieHzWorkspaceChip(const AerieHzWorkspaceChip&) = delete;
  AerieHzWorkspaceChip& operator=(const AerieHzWorkspaceChip&) = delete;
  ~AerieHzWorkspaceChip() override;

  const std::u16string& name() const { return name_; }

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  void OnPaint(gfx::Canvas* canvas) override;
  void OnThemeChanged() override;

  // aerie_tab_model::TabModelObserver:
  void OnWorkspaceChanged(const aerie_tab_model::Workspace& workspace) override;
  void OnModelLoaded() override;

 private:
  void Refresh();

  raw_ptr<aerie_tab_model::TabModel> model_ = nullptr;
  std::u16string name_;
};

// The Favorites tiles of the two-row layout, in the toolbar row (row 2). One
// tile for each Favorite of the Workspace, named by the tab's title; a press
// selects the tab. While it is in the toolbar it keeps the Favorites' tab views
// out of the strip, so a Favorite is in one place only. It is not built in the
// one-row layout, where there are no Favorites tiles.
class AerieHzFavoritesBar : public views::View,
                            public aerie_tab_model::TabModelObserver,
                            public TabStripModelObserver {
  METADATA_HEADER(AerieHzFavoritesBar, views::View)

 public:
  AerieHzFavoritesBar(BrowserView* browser_view, TabStripRegionView* strip);
  AerieHzFavoritesBar(const AerieHzFavoritesBar&) = delete;
  AerieHzFavoritesBar& operator=(const AerieHzFavoritesBar&) = delete;
  ~AerieHzFavoritesBar() override;

  size_t tile_count() const { return children().size(); }

  // views::View:
  void OnThemeChanged() override;

  // aerie_tab_model::TabModelObserver:
  void OnTabAdded(const aerie_tab_model::TabRecord& tab) override;
  void OnTabChanged(const aerie_tab_model::TabRecord& tab) override;
  void OnTabMoved(const aerie_tab_model::TabRecord& tab,
                  const aerie_tab_model::Placement& from) override;
  void OnTabRemoved(const aerie_tab_model::TabRecord& tab) override;
  void OnModelLoaded() override;

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;
  void OnTabChangedAt(tabs::TabInterface* tab,
                      TabChangeType change_type) override;

  // Builds the tiles again and hides the Favorites in the strip. Called on any
  // change; cheap (12 tiles at most).
  void Rebuild();

 private:
  void HideFavoritesInStrip();
  void ScheduleRebuild();

  const raw_ptr<BrowserView> browser_view_;
  const raw_ptr<TabStripRegionView> strip_;
  raw_ptr<aerie_tab_model::TabModel> model_ = nullptr;
  bool rebuild_pending_ = false;
  base::WeakPtrFactory<AerieHzFavoritesBar> weak_factory_{this};
};

namespace aerie_hz_chrome {

// Makes the layout's parts for the pref, and takes away the ones that do not
// belong. `horizontal_region` is the window's horizontal tab strip region (it
// exists in every layout), `toolbar` its toolbar. Called when the layout
// changes and when the window is made.
//   sidebar: none.   two rows: the chip and the Favorites bar.
//   one row: the chip, no Favorites bar.
void UpdateForLayout(BrowserView* browser_view,
                     TabStripRegionView* horizontal_region,
                     ToolbarView* toolbar);

}  // namespace aerie_hz_chrome

#endif  // CHROME_BROWSER_UI_VIEWS_AERIE_HZ_CHROME_AERIE_HZ_CHROME_H_
