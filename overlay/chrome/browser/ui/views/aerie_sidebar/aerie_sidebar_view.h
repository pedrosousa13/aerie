// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_VIEW_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_regions.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_toolbar_parts.h"
#include "components/aerie_design_tokens/aerie_motion.h"
#include "components/aerie_sidebar/aerie_sidebar_drag.h"
#include "components/aerie_sidebar/aerie_sidebar_palette.h"
#include "components/aerie_sidebar/aerie_sidebar_today_list.h"
#include "components/aerie_tab_model/tab_model_observer.h"
#include "components/prefs/pref_change_registrar.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/mojom/menu_source_type.mojom-forward.h"
#include "ui/gfx/geometry/point.h"
#include "ui/views/view.h"

class BrowserWindowInterface;
class AerieSidebarRowView;
class AerieSidebarTileView;
class AerieSidebarTopRow;
class AerieSidebarContextMenu;

namespace aerie_tab_model {
class PinnedTabsController;
class TabModel;
}  // namespace aerie_tab_model

namespace views {
class BoxLayoutView;
class Label;
class LabelButton;
class ScrollView;
}  // namespace views

namespace tabs {
class TabInterface;
}  // namespace tabs

// The contents of Aerie's sidebar (issues #177, #215 and #180), top to bottom:
// the top row (window controls, the sidebar toggle, Back, Forward, Reload), the
// address pill (the real location bar), the Favorites grid (one tile per
// Favorite, in `size-favorites-columns` columns), the Pinned list (one row per
// pinned tab, with the "Back to pinned page" button), a New Tab row, the Today
// list, one row per unpinned tab of this window, and the bottom bar (a
// placeholder; the toolbar's remaining buttons live there for now). The lists
// are kept from the Tab Model (aerie_sidebar::TodayList, one per section).
// A short message (the toast) shows above the bottom bar for a moment, for the
// 13th Favorite. It is a child of VerticalTabStripRegionView,
// which keeps owning the frame of the column: its slot in the window layout,
// its background, its resize area and its width. docs/sidebar-design.md,
// "Native sidebar view", has the reasons.
//
// The view reads tabs from the window's TabStripModel and asks it to select or
// close them; it does not move tabs.
class AerieSidebarView : public views::View,
                         public TabStripModelObserver,
                         public aerie_tab_model::TabModelObserver,
                         public aerie_sidebar::TodayList::Delegate {
  METADATA_HEADER(AerieSidebarView, views::View)

 public:
  // `new_tab` opens a new tab in the window.
  AerieSidebarView(BrowserWindowInterface* browser,
                   base::RepeatingClosure new_tab);
  AerieSidebarView(const AerieSidebarView&) = delete;
  AerieSidebarView& operator=(const AerieSidebarView&) = delete;
  ~AerieSidebarView() override;

  // The rows, top to bottom, for tests and the screenshot driver.
  size_t row_count() const { return rows_.size(); }

  // Takes the toolbar's parts in (see AerieSidebarToolbarParts). Call once,
  // after the toolbar has made its buttons.
  void AdoptToolbar(const AerieSidebarToolbarParts& parts);
  bool has_toolbar_parts() const { return !!adopted_toolbar_; }

  // `leading_inset` is the room the platform's own window controls take at
  // the leading edge of the window; `draw_window_controls` asks the top row to
  // draw its own (the frame draws them on the far side of the window).
  void SetWindowControls(int leading_inset, bool draw_window_controls);

  // True when `point` (in this view's coordinates) is the empty part of the top
  // row, which the window manager treats as the title bar.
  bool IsPositionInWindowCaption(const gfx::Point& point) const;

  // The regions, for tests and for the layout that follows.
  AerieSidebarTopRow* top_row() const { return top_row_; }
  views::View* address_pill() const { return pill_; }
  views::View* bottom_bar() const { return bottom_bar_; }

  // F6 (issue #215): the regions of the sidebar that hold keyboard focus, in
  // focus order: the top row, the address pill, the Today tree and, last, what
  // is left of the toolbar in the bottom bar. BrowserView::GetAccessiblePanes
  // puts them where it used to put the toolbar and the tab strip, so the page
  // follows them.
  void GetAccessiblePanes(std::vector<views::View*>* panes);

  // True while a row is being dragged, and the slot it would take, for tests.
  bool dragging() const { return drag_.active; }
  int drag_target_index() const { return drag_.target; }

  // The Pinned list and the Favorites grid (issue #180), for tests.
  size_t pinned_row_count() const { return pinned_rows_.size(); }
  AerieSidebarRowView* pinned_row_at(size_t index) const;
  std::string pinned_tab_id_at(size_t index) const;
  AerieSidebarTodayTree* pinned_tree() const { return pinned_list_; }
  AerieSidebarRowView* pinned_tab_stop_row() const;
  size_t favorite_tile_count() const { return tiles_.size(); }
  AerieSidebarTileView* tile_at(size_t index) const;
  std::string favorite_tab_id_at(size_t index) const;
  AerieSidebarFavoritesGrid* favorites_grid() const { return favorites_grid_; }
  AerieSidebarTileView* tile_stop() const;
  // The short message above the bottom bar, and whether it shows now.
  views::Label* toast() const { return toast_; }
  bool toast_visible() const;

  // The rows, in order, for tests: the live ones, then the ones that are
  // closing.
  AerieSidebarRowView* row_at(size_t index) const;
  // The Tab Model record id of the row at `index`, for tests.
  std::string tab_id_at(size_t index) const;
  // Rows that have left the Today list and are still collapsing.
  size_t closing_row_count() const { return closing_rows_.size(); }
  AerieSidebarRowView* closing_row_at(size_t index) const;
  // The row that holds the roving tab stop, or null when the list is empty.
  AerieSidebarRowView* tab_stop_row() const;
  AerieSidebarTodayTree* today_tree() const { return list_; }

  // The colours the sidebar draws with now: the theme, the colour mode of the
  // window and the Workspace colour (issue #215, item 7). They follow a change
  // of either at once.
  const aerie_sidebar::SidebarPalette& palette() const { return palette_; }

  // aerie_sidebar::TodayList::Delegate:
  void OnRowInserted(int index, const std::string& tab_id) override;
  void OnRowRemoved(int index, const std::string& tab_id) override;
  void OnRowsReset() override;

  // aerie_tab_model::TabModelObserver:
  void OnWorkspaceChanged(const aerie_tab_model::Workspace& workspace) override;
  void OnModelLoaded() override;
  void OnTabAdded(const aerie_tab_model::TabRecord& tab) override;
  void OnTabMoved(const aerie_tab_model::TabRecord& tab,
                  const aerie_tab_model::Placement& from) override;
  void OnTabChanged(const aerie_tab_model::TabRecord& tab) override;
  void OnTabRemoved(const aerie_tab_model::TabRecord& tab) override;

  // The text at the end of the Today heading (the number of sleeping tabs and
  // the memory estimate, issue #179), and whether it shows. For tests.
  views::Label* sleeping_summary() const { return sleeping_summary_; }
  // The context menu of a row while it is open, or null.
  AerieSidebarContextMenu* context_menu() const { return context_menu_.get(); }

  // views::View:
  void OnThemeChanged() override;

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;
  void OnTabChangedAt(tabs::TabInterface* tab,
                      TabChangeType change_type) override;

 private:
  struct Row {
    std::string tab_id;
    raw_ptr<AerieSidebarRowView> view;
  };
  struct Tile {
    std::string tab_id;
    raw_ptr<AerieSidebarTileView> view;
  };
  // Tells the view that the Favorites or the Pinned list changed.
  class SectionNotifier;

  // The tab of this window that has this record id, or null.
  tabs::TabInterface* FindTab(const std::string& tab_id) const;
  // Makes every section match the Tab Model. When a tab changed section, the
  // rows move to their new places with the FLIP motion (issue #180).
  void SyncAll();
  void SyncFavorites();
  void SyncPinned();
  // Makes the rows match the Today list, keeping the views of rows that stay.
  void SyncRows();
  // Where every row and tile is now, in this view's coordinates, by tab id.
  std::map<std::string, gfx::Point> CapturePositions() const;
  // The FLIP motion: from the places in `before` to the places now.
  void PlayFlip(const std::map<std::string, gfx::Point>& before,
                const std::string& moved_id);
  // The Tab Model tells its observers before the tab strip adapter has joined
  // the new record to its tab, so the rows are synced in a task after the
  // change, and several changes make one sync.
  void ScheduleSync();
  void UpdateRows();
  void UpdateRow(const Row& row);
  void UpdateTile(const Tile& tile);
  void SelectTab(const std::string& tab_id);
  // The context menu of a row or a tile (issues #179 and #180): Pin or Unpin,
  // Add to or Remove from Favorites, Sleep tab and Close tab.
  void ShowRowContextMenu(const std::string& tab_id,
                          const gfx::Point& screen_point,
                          ui::mojom::MenuSourceType source_type);
  void OnContextCommand(const std::string& tab_id, int command);
  // Shows the toast for `kMotionTimingToast` ms and says it politely to a
  // screen reader.
  void ShowToast(const std::u16string& text);
  void HideToast();
  aerie_tab_model::PinnedTabsController* PinnedController() const;
  void GoBackToPinnedPage(const std::string& tab_id);
  // Writes the count of sleeping tabs and the memory estimate into the Today
  // header, or hides them when no tab sleeps.
  void UpdateSleepingSummary();
  void CloseTab(const std::string& tab_id);
  // A key of the Today tree on the row of `tab_id` (arrows, Home, End, Delete).
  void OnRowKey(const std::string& tab_id, aerie_sidebar::ListKey key);
  // Keyboard focus is on a row: it is the Tab stop from now on.
  void OnRowFocused();
  // The same for the Pinned tree and the Favorites grid (issue #180).
  void OnPinnedRowKey(const std::string& tab_id, aerie_sidebar::ListKey key);
  void OnPinnedRowFocused();
  void UpdatePinnedTabStop();
  void FocusPinnedRow(int index);
  void FocusPinnedTabStop();
  int IndexOfPinnedRow(const std::string& tab_id) const;
  void OnTileKey(const std::string& tab_id, aerie_sidebar::GridKey key);
  void OnTileFocused();
  void UpdateTileTabStop();
  void FocusTile(int index);
  void FocusTileStop();
  int IndexOfTile(const std::string& tab_id) const;
  // Gives the Tab stop to the focused row, else to the selected row, else to
  // the first one.
  void UpdateTabStop();
  // Marks row `index` as the Tab stop and moves keyboard focus to it.
  void FocusRow(int index);
  // Moves focus to the Tab stop (what F6 and the tree's RequestFocus do).
  void FocusTabStop();
  int IndexOfRow(const std::string& tab_id) const;
  // True when rows animate: the window is shown and no session is being
  // restored (docs/sidebar-design.md, "Performance rules", rule 3).
  bool CanAnimateRows() const;
  // The side of the window the sidebar is on: -1 left, +1 right.
  int OuterEdgeSign() const;
  void RemoveClosedRow(AerieSidebarRowView* row);

  void ToggleSidebar();

  // Dragging a row to reorder it (issue #215, item 8; the geometry is
  // aerie_sidebar::DragTargetIndex and its neighbours). The dragged row follows
  // the pointer and lifts, the rows it passes shift one slot, and a drop moves
  // the tab in the window's tab strip: the Tab Model follows the strip
  // (TabStripAdapter), so the list follows the Tab Model. Dropping outside the
  // window moves the tab to a window of its own.
  //
  // Issue #182 (drag between Today, Pinned, folders and Favorites) starts
  // here: it replaces the target computation in OnRowDragUpdate and the move
  // in MoveTodayRow with a drop target that names a section, a parent and a
  // position, and moves the record in the Tab Model.
  bool OnRowDragBegin(const std::string& tab_id);
  void OnRowDragUpdate(const std::string& tab_id, int dy);
  void OnRowDragEnd(const std::string& tab_id,
                    int dy,
                    bool cancelled,
                    bool outside);
  // Puts every row at its slot at once and forgets the drag.
  void CancelDrag();
  // Moves the tab of row `source` to the place of row `target` in the strip and
  // brings the rows into the new order at once.
  void MoveTodayRow(int source, int target);
  // Works the palette out for the window's colour mode and the Workspace colour
  // and hands it to every view that draws with it. Does nothing when it did not
  // change.
  void ApplyPalette();
  aerie_sidebar::SidebarPalette ResolvePalette() const;

  const raw_ptr<BrowserWindowInterface> browser_;
  base::RepeatingClosure new_tab_;
  raw_ptr<AerieSidebarTopRow> top_row_ = nullptr;
  raw_ptr<AerieSidebarPill> pill_ = nullptr;
  raw_ptr<views::View> bottom_bar_ = nullptr;
  // The toolbar and its parts that moved in. They go back to the toolbar when
  // the sidebar goes, so that the toolbar never points at a view that is gone.
  raw_ptr<views::View> adopted_toolbar_ = nullptr;
  std::vector<raw_ptr<views::View>> adopted_parts_;
  raw_ptr<aerie_tab_model::TabModel> model_ = nullptr;
  aerie_sidebar::SidebarPalette palette_;
  raw_ptr<views::Label> today_heading_ = nullptr;
  raw_ptr<views::Label> sleeping_summary_ = nullptr;
  std::unique_ptr<AerieSidebarContextMenu> context_menu_;
  raw_ptr<views::LabelButton> new_tab_row_ = nullptr;
  raw_ptr<AerieSidebarTodayTree> list_ = nullptr;
  raw_ptr<views::ScrollView> scroll_ = nullptr;
  std::vector<Row> rows_;
  // The Favorites grid and the Pinned list (issue #180).
  raw_ptr<AerieSidebarFavoritesGrid> favorites_grid_ = nullptr;
  std::vector<Tile> tiles_;
  raw_ptr<views::ScrollView> pinned_scroll_ = nullptr;
  raw_ptr<AerieSidebarTodayTree> pinned_list_ = nullptr;
  std::vector<Row> pinned_rows_;
  raw_ptr<views::Label> toast_ = nullptr;
  base::OneShotTimer toast_timer_;
  aerie::motion::MotionHandle toast_motion_;
  PrefChangeRegistrar pref_registrar_;
  // A tab changed section: the next sync plays the FLIP motion for it.
  bool flip_pending_ = false;
  std::map<std::string, gfx::Point> flip_before_;
  std::string flip_moved_id_;
  // The drag in progress, if any.
  struct DragState {
    bool active = false;
    std::string tab_id;
    int source = -1;
    int target = -1;
    // The rows' slots when the drag began.
    std::vector<aerie_sidebar::DragSlot> slots;
  };
  DragState drag_;
  // Rows whose tab has gone and that are still sliding out and collapsing. They
  // stay in the list so that the others do not jump, and take no input.
  std::vector<raw_ptr<AerieSidebarRowView>> closing_rows_;
  std::unique_ptr<aerie_sidebar::TodayList> today_list_;
  std::unique_ptr<SectionNotifier> favorites_notifier_;
  std::unique_ptr<SectionNotifier> pinned_notifier_;
  std::unique_ptr<aerie_sidebar::TodayList> favorites_list_;
  std::unique_ptr<aerie_sidebar::TodayList> pinned_list_model_;
  bool sync_pending_ = false;
  // The tab that Delete or Ctrl+W asked to close from a row: when its row goes,
  // keyboard focus stays in the list (closing the selected tab would otherwise
  // hand focus to the page).
  std::string refocus_after_close_id_;
  std::string pinned_refocus_after_close_id_;
  base::WeakPtrFactory<AerieSidebarView> weak_factory_{this};
};

#endif  // CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_VIEW_H_
