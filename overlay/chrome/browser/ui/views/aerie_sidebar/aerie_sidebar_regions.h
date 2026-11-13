// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_REGIONS_H_
#define CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_REGIONS_H_

#include "base/functional/callback.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/layout/box_layout_view.h"
#include "ui/views/view.h"

// Two regions of the sidebar that F6 moves focus to (issue #215): F6 asks the
// region to take focus (View::RequestFocus), and a plain view does not know
// which of its children to give it to. The top row is an AccessiblePaneView
// and needs no helper.

// The address pill. Focus goes to the omnibox inside it.
class AerieSidebarPill : public views::View {
  METADATA_HEADER(AerieSidebarPill, views::View)

 public:
  AerieSidebarPill();
  AerieSidebarPill(const AerieSidebarPill&) = delete;
  AerieSidebarPill& operator=(const AerieSidebarPill&) = delete;
  ~AerieSidebarPill() override;

  // views::View:
  void RequestFocus() override;
};

// The list of the Today rows. Focus goes to the row that holds the roving tab
// stop, which the sidebar knows.
class AerieSidebarTodayTree : public views::BoxLayoutView {
  METADATA_HEADER(AerieSidebarTodayTree, views::BoxLayoutView)

 public:
  AerieSidebarTodayTree();
  AerieSidebarTodayTree(const AerieSidebarTodayTree&) = delete;
  AerieSidebarTodayTree& operator=(const AerieSidebarTodayTree&) = delete;
  ~AerieSidebarTodayTree() override;

  void SetFocusHandler(base::RepeatingClosure focus_tab_stop);

  // views::View:
  void RequestFocus() override;

 private:
  base::RepeatingClosure focus_tab_stop_;
};

// The Favorites grid (issue #180): its tiles in `size-favorites-columns` (4)
// columns, left to right, top to bottom, each as wide as the room allows and
// `size-tile-comfortable` high. Focus goes to the tile that holds the roving tab
// stop, which the sidebar knows.
class AerieSidebarFavoritesGrid : public views::View {
  METADATA_HEADER(AerieSidebarFavoritesGrid, views::View)

 public:
  AerieSidebarFavoritesGrid();
  AerieSidebarFavoritesGrid(const AerieSidebarFavoritesGrid&) = delete;
  AerieSidebarFavoritesGrid& operator=(const AerieSidebarFavoritesGrid&) =
      delete;
  ~AerieSidebarFavoritesGrid() override;

  void SetFocusHandler(base::RepeatingClosure focus_tab_stop);
  // The columns the grid lays out in.
  int columns() const;
  // A least height, so that an empty grid can take a drop (issue #182); 0
  // puts it away.
  void SetMinHeight(int height);

  // views::View:
  void RequestFocus() override;
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  void Layout(PassKey) override;

 private:
  base::RepeatingClosure focus_tab_stop_;
  int min_height_ = 0;
};

#endif  // CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_REGIONS_H_
