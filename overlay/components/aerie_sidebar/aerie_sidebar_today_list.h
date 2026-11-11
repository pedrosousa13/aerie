// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_SIDEBAR_AERIE_SIDEBAR_TODAY_LIST_H_
#define COMPONENTS_AERIE_SIDEBAR_AERIE_SIDEBAR_TODAY_LIST_H_

#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "components/aerie_tab_model/tab_model_observer.h"

namespace aerie_tab_model {
class TabModel;
}

namespace aerie_sidebar {

// The keys of the Today tree (docs/sidebar-design.md, "Keyboard and screen
// reader"): arrows move the one tab stop, Home and End jump, Delete closes.
enum class ListKey { kUp, kDown, kHome, kEnd, kDelete };

struct ListKeyResult {
  // The row that holds the tab stop afterwards; -1 when the list is empty.
  int focus_index = -1;
  // Delete: close the tab of the row that was focused.
  bool close_focused = false;
};

// Up and Down stop at the ends (a tree does not wrap). After Delete the stop
// stays at the same index, so it lands on the next row, or on the new last row
// when the closed one was last. `focused` outside 0..count-1 counts as the
// first row.
ListKeyResult HandleListKey(int count, int focused, ListKey key);

// The keys of the Favorites grid (issue #180): arrows move the one tab stop in
// two dimensions, Home and End go to the first and last tile of the row, with
// Ctrl they go to the first and last tile of the grid, Delete closes.
enum class GridKey {
  kLeft,
  kRight,
  kUp,
  kDown,
  kRowStart,
  kRowEnd,
  kGridStart,
  kGridEnd,
  kDelete,
};

// `columns` tiles fill a row, left to right. Nothing wraps: Left on the first
// tile of a row and Right on the last stay put, Up on the first row and Down on
// the last stay put, and Down from a tile with no tile below it (the last row is
// short) lands on the last tile. After Delete the stop stays at the same index,
// or on the new last tile.
ListKeyResult HandleGridKey(int count, int columns, int focused, GridKey key);

// The rows of the Today list, in order, kept from the Tab Model through
// TabModelObserver (issue #177). A row is a tab of the Workspace in the Today
// section that is not in a folder and not archived. Folders come with a later
// slice, so a tab inside one is not listed yet. Headless: the view implements
// Delegate and builds or removes one row per call.
class TodayList : public aerie_tab_model::TabModelObserver {
 public:
  class Delegate {
   public:
    // One row was added at, or removed from, `index`. A row of the list
    // moved or several changed at once: OnRowsReset, read rows() again.
    virtual void OnRowInserted(int index, const std::string& tab_id) = 0;
    virtual void OnRowRemoved(int index, const std::string& tab_id) = 0;
    virtual void OnRowsReset() = 0;

   protected:
    ~Delegate() = default;
  };

  // `model` and `delegate` must outlive the list. `section` is the section the
  // list shows: Today by default; the Pinned section and the Favorites grid
  // (issue #180) use the same list over their own section.
  TodayList(aerie_tab_model::TabModel* model,
            Delegate* delegate,
            aerie_tab_model::Section section = aerie_tab_model::Section::kToday);
  TodayList(const TodayList&) = delete;
  TodayList& operator=(const TodayList&) = delete;
  ~TodayList() override;

  // Tab ids, ordered by position, then by id.
  const std::vector<std::string>& rows() const { return rows_; }

  // aerie_tab_model::TabModelObserver:
  void OnTabAdded(const aerie_tab_model::TabRecord& tab) override;
  void OnTabChanged(const aerie_tab_model::TabRecord& tab) override;
  void OnTabMoved(const aerie_tab_model::TabRecord& tab,
                  const aerie_tab_model::Placement& from) override;
  void OnTabRemoved(const aerie_tab_model::TabRecord& tab) override;
  void OnWorkspaceChanged(const aerie_tab_model::Workspace& workspace) override;
  void OnModelLoaded() override;
  // A folder change can move tabs out of a folder: the parent of a tab is
  // updated by the model, which tells OnTabMoved; nothing else is needed.

 private:
  std::vector<std::string> Compute() const;
  void Refresh();

  const raw_ptr<aerie_tab_model::TabModel> model_;
  const raw_ptr<Delegate> delegate_;
  const aerie_tab_model::Section section_;
  std::vector<std::string> rows_;
};

}  // namespace aerie_sidebar

#endif  // COMPONENTS_AERIE_SIDEBAR_AERIE_SIDEBAR_TODAY_LIST_H_
