// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_sidebar/aerie_sidebar_today_list.h"

#include <algorithm>
#include <utility>

#include "components/aerie_tab_model/tab_model.h"

namespace aerie_sidebar {

ListKeyResult HandleListKey(int count, int focused, ListKey key) {
  ListKeyResult result;
  if (count <= 0) {
    return result;
  }
  const int at = (focused < 0 || focused >= count) ? 0 : focused;
  switch (key) {
    case ListKey::kUp:
      result.focus_index = std::max(at - 1, 0);
      break;
    case ListKey::kDown:
      result.focus_index = std::min(at + 1, count - 1);
      break;
    case ListKey::kHome:
      result.focus_index = 0;
      break;
    case ListKey::kEnd:
      result.focus_index = count - 1;
      break;
    case ListKey::kDelete:
      result.close_focused = true;
      result.focus_index = std::min(at, count - 2);
      break;
  }
  return result;
}

TodayList::TodayList(aerie_tab_model::TabModel* model, Delegate* delegate)
    : model_(model), delegate_(delegate) {
  rows_ = Compute();
  model_->AddObserver(this);
}

TodayList::~TodayList() {
  model_->RemoveObserver(this);
}

std::vector<std::string> TodayList::Compute() const {
  std::vector<const aerie_tab_model::TabRecord*> tabs;
  for (const auto& [id, tab] : model_->tabs()) {
    if (tab.workspace_id == model_->workspace().id &&
        tab.section == aerie_tab_model::Section::kToday &&
        tab.parent.empty() && !tab.archived_at.has_value()) {
      tabs.push_back(&tab);
    }
  }
  std::sort(tabs.begin(), tabs.end(), [](const auto* a, const auto* b) {
    return a->position != b->position ? a->position < b->position
                                      : a->id < b->id;
  });
  std::vector<std::string> ids;
  for (const auto* tab : tabs) {
    ids.push_back(tab->id);
  }
  return ids;
}

void TodayList::Refresh() {
  std::vector<std::string> next = Compute();
  if (next == rows_) {
    return;
  }
  std::vector<std::string> old = std::move(rows_);
  rows_ = std::move(next);
  if (rows_.size() == old.size() + 1) {
    // One row was inserted when removing it gives the old list back.
    for (size_t i = 0; i < rows_.size(); ++i) {
      std::vector<std::string> without = rows_;
      without.erase(without.begin() + i);
      if (without == old) {
        delegate_->OnRowInserted(static_cast<int>(i), rows_[i]);
        return;
      }
    }
  } else if (old.size() == rows_.size() + 1) {
    for (size_t i = 0; i < old.size(); ++i) {
      std::vector<std::string> without = old;
      without.erase(without.begin() + i);
      if (without == rows_) {
        delegate_->OnRowRemoved(static_cast<int>(i), old[i]);
        return;
      }
    }
  }
  delegate_->OnRowsReset();
}

void TodayList::OnTabAdded(const aerie_tab_model::TabRecord&) {
  Refresh();
}
void TodayList::OnTabChanged(const aerie_tab_model::TabRecord&) {
  Refresh();
}
void TodayList::OnTabMoved(const aerie_tab_model::TabRecord&,
                           const aerie_tab_model::Placement&) {
  Refresh();
}
void TodayList::OnTabRemoved(const aerie_tab_model::TabRecord&) {
  Refresh();
}
void TodayList::OnWorkspaceChanged(const aerie_tab_model::Workspace&) {
  Refresh();
}
void TodayList::OnModelLoaded() {
  Refresh();
}

}  // namespace aerie_sidebar
