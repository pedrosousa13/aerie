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
#include "components/aerie_sidebar/aerie_sidebar_today_list.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"

class BrowserWindowInterface;
class AerieSidebarRowView;

namespace views {
class BoxLayoutView;
class ScrollView;
}  // namespace views

namespace tabs {
class TabInterface;
}  // namespace tabs

// The contents of Aerie's sidebar (issue #177): a New Tab row and the Today
// list, one row per unpinned tab of this window, kept from the Tab Model
// (aerie_sidebar::TodayList). It is a child of VerticalTabStripRegionView,
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

  const raw_ptr<BrowserWindowInterface> browser_;
  base::RepeatingClosure new_tab_;
  raw_ptr<views::BoxLayoutView> list_ = nullptr;
  raw_ptr<views::ScrollView> scroll_ = nullptr;
  std::vector<Row> rows_;
  std::unique_ptr<aerie_sidebar::TodayList> today_list_;
  bool sync_pending_ = false;
  base::WeakPtrFactory<AerieSidebarView> weak_factory_{this};
};

#endif  // CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_VIEW_H_
