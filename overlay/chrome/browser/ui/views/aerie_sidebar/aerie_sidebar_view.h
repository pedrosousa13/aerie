// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_VIEW_H_

#include <memory>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_regions.h"
#include "components/aerie_sidebar/aerie_sidebar_today_list.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"

class BrowserWindowInterface;
class AerieSidebarRowView;
class AerieSidebarTopRow;

namespace views {
class BoxLayoutView;
class ScrollView;
}  // namespace views

namespace tabs {
class TabInterface;
}  // namespace tabs

// The contents of Aerie's sidebar (issues #177 and #215), top to bottom: the top
// row (window controls, the sidebar toggle, Back, Forward, Reload), the address
// pill (the real location bar), a New Tab row, the Today list, one row per
// unpinned tab of this window, kept from the Tab Model
// (aerie_sidebar::TodayList), and the bottom bar (a placeholder; the toolbar's
// remaining buttons live there for now). It is a child of VerticalTabStripRegionView,
// which keeps owning the frame of the column: its slot in the window layout,
// its background, its resize area and its width. docs/sidebar-design.md,
// "Native sidebar view", has the reasons.
//
// The view reads tabs from the window's TabStripModel and asks it to select or
// close them; it does not move tabs.
class AerieSidebarView : public views::View,
                         public TabStripModelObserver,
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

  // What the sidebar takes out of the browser's toolbar (issue #215). The
  // views are the toolbar's own, not copies: the address pill is the real
  // location bar, so the omnibox, the page actions and the Guard icon keep
  // working, and the buttons keep their commands and menus. All are plain
  // views so that this target does not depend on the toolbar's.
  struct ToolbarParts {
    raw_ptr<views::View> toolbar;       // What stays after the rest left.
    raw_ptr<views::View> location_bar;  // Goes into the address pill.
    raw_ptr<views::View> back;          // These three go into the top row.
    raw_ptr<views::View> forward;
    raw_ptr<views::View> reload;
  };
  // Moves the parts in. Call once, after the toolbar has made its buttons.
  void AdoptToolbar(const ToolbarParts& parts);
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

  // The rows, in order, for tests: the live ones, then the ones that are
  // closing.
  AerieSidebarRowView* row_at(size_t index) const;
  // Rows that have left the Today list and are still collapsing.
  size_t closing_row_count() const { return closing_rows_.size(); }
  AerieSidebarRowView* closing_row_at(size_t index) const;
  // The row that holds the roving tab stop, or null when the list is empty.
  AerieSidebarRowView* tab_stop_row() const;
  AerieSidebarTodayTree* today_tree() const { return list_; }

  // aerie_sidebar::TodayList::Delegate:
  void OnRowInserted(int index, const std::string& tab_id) override;
  void OnRowRemoved(int index, const std::string& tab_id) override;
  void OnRowsReset() override;

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

  // The tab of this window that has this record id, or null.
  tabs::TabInterface* FindTab(const std::string& tab_id) const;
  // Makes the rows match the Today list, keeping the views of rows that stay.
  void SyncRows();
  // The Tab Model tells its observers before the tab strip adapter has joined
  // the new record to its tab, so the rows are synced in a task after the
  // change, and several changes make one sync.
  void ScheduleSync();
  void UpdateRows();
  void UpdateRow(const Row& row);
  void SelectTab(const std::string& tab_id);
  void CloseTab(const std::string& tab_id);
  // A key of the Today tree on the row of `tab_id` (arrows, Home, End, Delete).
  void OnRowKey(const std::string& tab_id, aerie_sidebar::ListKey key);
  // Keyboard focus is on a row: it is the Tab stop from now on.
  void OnRowFocused();
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

  const raw_ptr<BrowserWindowInterface> browser_;
  base::RepeatingClosure new_tab_;
  raw_ptr<AerieSidebarTopRow> top_row_ = nullptr;
  raw_ptr<AerieSidebarPill> pill_ = nullptr;
  raw_ptr<views::View> bottom_bar_ = nullptr;
  // The toolbar and its parts that moved in. They go back to the toolbar when
  // the sidebar goes, so that the toolbar never points at a view that is gone.
  raw_ptr<views::View> adopted_toolbar_ = nullptr;
  std::vector<raw_ptr<views::View>> adopted_parts_;
  raw_ptr<AerieSidebarTodayTree> list_ = nullptr;
  raw_ptr<views::ScrollView> scroll_ = nullptr;
  std::vector<Row> rows_;
  // Rows whose tab has gone and that are still sliding out and collapsing. They
  // stay in the list so that the others do not jump, and take no input.
  std::vector<raw_ptr<AerieSidebarRowView>> closing_rows_;
  std::unique_ptr<aerie_sidebar::TodayList> today_list_;
  bool sync_pending_ = false;
  // The tab that Delete or Ctrl+W asked to close from a row: when its row goes,
  // keyboard focus stays in the list (closing the selected tab would otherwise
  // hand focus to the page).
  std::string refocus_after_close_id_;
  base::WeakPtrFactory<AerieSidebarView> weak_factory_{this};
};

#endif  // CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_VIEW_H_
