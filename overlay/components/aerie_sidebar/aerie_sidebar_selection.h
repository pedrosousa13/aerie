// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_SIDEBAR_AERIE_SIDEBAR_SELECTION_H_
#define COMPONENTS_AERIE_SIDEBAR_AERIE_SIDEBAR_SELECTION_H_

#include <optional>
#include <set>
#include <string>
#include <vector>

#include "components/aerie_tab_model/tab_model_types.h"

// Which rows of the sidebar are selected together (issue #265). Headless: the
// view tells it the visible rows and forwards the clicks and keys, and asks it
// which rows to draw as selected and which tabs a batch action covers.
//
// The selection lives in one section (Favorites, Pinned or Today). A click in
// another section starts a new selection there, and a range never crosses a
// section. Only tab rows can be selected; a folder row is in the list so that
// a range passes over it, and the tabs of an open folder are rows like any
// other. This is not the open tab of the window: that one is marked by the
// tab strip.
namespace aerie_sidebar {

struct SelectionRow {
  std::string id;
  aerie_tab_model::Section section = aerie_tab_model::Section::kToday;
  // False for a folder row.
  bool selectable = true;
};

class SelectionModel {
 public:
  SelectionModel();
  ~SelectionModel();

  // The visible rows in display order: Favorites, Pinned, Today. A selected
  // row that is no longer in the list leaves the selection (its tab closed,
  // moved to the Archive or hidden by a shut folder).
  void SetRows(std::vector<SelectionRow> rows);

  // Ctrl+click: flips the row and makes it the anchor. Returns false for a row
  // that cannot be selected.
  bool Toggle(const std::string& id);
  // Shift+click: the selection becomes the rows from the anchor to `id`. With
  // no anchor, or an anchor in another section, it is `id` alone.
  bool SelectRange(const std::string& id);
  // Esc, or a plain click.
  void Clear();
  // Ctrl+A: every tab of `section`. False when the section has none.
  bool SelectAll(aerie_tab_model::Section section);
  // Shift+Up (-1) and Shift+Down (+1) from the focused row `focused`: moves the
  // far end of the range one selectable row. Returns the row that now holds
  // the far end (where focus goes), or an empty string at the edge of the
  // section.
  std::string Extend(const std::string& focused, int direction);

  bool IsSelected(const std::string& id) const;
  bool empty() const { return selected_.empty(); }
  size_t size() const { return selected_.size(); }
  // The selected ids in display order.
  std::vector<std::string> Selected() const;
  // The section of the selection; none when it is empty.
  std::optional<aerie_tab_model::Section> section() const;
  const std::string& anchor() const { return anchor_; }

 private:
  int IndexOf(const std::string& id) const;
  void SelectBetween(int from, int to);
  void ForgetAnchors();

  std::vector<SelectionRow> rows_;
  std::set<std::string> selected_;
  std::string anchor_;
  // The end of the range that moves.
  std::string active_;
};

}  // namespace aerie_sidebar

#endif  // COMPONENTS_AERIE_SIDEBAR_AERIE_SIDEBAR_SELECTION_H_
