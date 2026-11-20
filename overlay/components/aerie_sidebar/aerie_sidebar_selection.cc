// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_sidebar/aerie_sidebar_selection.h"

#include <algorithm>
#include <utility>

namespace aerie_sidebar {

SelectionModel::SelectionModel() = default;
SelectionModel::~SelectionModel() = default;

int SelectionModel::IndexOf(const std::string& id) const {
  for (size_t i = 0; i < rows_.size(); ++i) {
    if (rows_[i].id == id) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

void SelectionModel::ForgetAnchors() {
  anchor_.clear();
  active_.clear();
}

void SelectionModel::SetRows(std::vector<SelectionRow> rows) {
  rows_ = std::move(rows);
  std::set<std::string> kept;
  for (const SelectionRow& row : rows_) {
    if (row.selectable && selected_.count(row.id)) {
      kept.insert(row.id);
    }
  }
  selected_ = std::move(kept);
  if (selected_.empty()) {
    ForgetAnchors();
    return;
  }
  // An anchor whose row has gone falls back to the first row that is left.
  const std::vector<std::string> left = Selected();
  if (IndexOf(anchor_) < 0) {
    anchor_ = left.front();
  }
  if (IndexOf(active_) < 0) {
    active_ = left.back();
  }
}

bool SelectionModel::Toggle(const std::string& id) {
  const int index = IndexOf(id);
  if (index < 0 || !rows_[index].selectable) {
    return false;
  }
  if (!empty() && section() != rows_[index].section) {
    selected_.clear();
  }
  if (!selected_.erase(id)) {
    selected_.insert(id);
  }
  if (selected_.empty()) {
    ForgetAnchors();
  } else {
    anchor_ = id;
    active_ = id;
  }
  return true;
}

void SelectionModel::SelectBetween(int from, int to) {
  selected_.clear();
  const aerie_tab_model::Section in = rows_[from].section;
  for (int i = std::min(from, to); i <= std::max(from, to); ++i) {
    if (rows_[i].selectable && rows_[i].section == in) {
      selected_.insert(rows_[i].id);
    }
  }
}

bool SelectionModel::SelectRange(const std::string& id) {
  const int index = IndexOf(id);
  if (index < 0 || !rows_[index].selectable) {
    return false;
  }
  const int anchor = anchor_.empty() ? -1 : IndexOf(anchor_);
  if (anchor < 0 || rows_[anchor].section != rows_[index].section) {
    selected_.clear();
    selected_.insert(id);
    anchor_ = id;
    active_ = id;
    return true;
  }
  SelectBetween(anchor, index);
  active_ = id;
  return true;
}

void SelectionModel::Clear() {
  selected_.clear();
  ForgetAnchors();
}

bool SelectionModel::SelectAll(aerie_tab_model::Section section) {
  std::vector<std::string> ids;
  for (const SelectionRow& row : rows_) {
    if (row.selectable && row.section == section) {
      ids.push_back(row.id);
    }
  }
  if (ids.empty()) {
    return false;
  }
  selected_ = std::set<std::string>(ids.begin(), ids.end());
  anchor_ = ids.front();
  active_ = ids.back();
  return true;
}

std::string SelectionModel::Extend(const std::string& focused, int direction) {
  const int at = IndexOf(focused);
  if (at < 0 || !rows_[at].selectable || direction == 0) {
    return std::string();
  }
  if (empty() || active_ != focused || section() != rows_[at].section) {
    selected_.clear();
    selected_.insert(focused);
    anchor_ = focused;
    active_ = focused;
  }
  const int step = direction > 0 ? 1 : -1;
  for (int i = at + step; i >= 0 && i < static_cast<int>(rows_.size());
       i += step) {
    if (rows_[i].section != rows_[at].section) {
      break;
    }
    if (!rows_[i].selectable) {
      continue;
    }
    SelectBetween(IndexOf(anchor_), i);
    active_ = rows_[i].id;
    return active_;
  }
  return std::string();
}

bool SelectionModel::IsSelected(const std::string& id) const {
  return selected_.count(id) > 0;
}

std::vector<std::string> SelectionModel::Selected() const {
  std::vector<std::string> ids;
  for (const SelectionRow& row : rows_) {
    if (selected_.count(row.id)) {
      ids.push_back(row.id);
    }
  }
  return ids;
}

std::optional<aerie_tab_model::Section> SelectionModel::section() const {
  for (const SelectionRow& row : rows_) {
    if (selected_.count(row.id)) {
      return row.section;
    }
  }
  return std::nullopt;
}

}  // namespace aerie_sidebar
