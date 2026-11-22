// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_sidebar/aerie_sidebar_recent_tabs.h"

#include <algorithm>

namespace aerie_sidebar {

RecentTabs::RecentTabs() = default;
RecentTabs::RecentTabs(const RecentTabs&) = default;
RecentTabs& RecentTabs::operator=(const RecentTabs&) = default;
RecentTabs::~RecentTabs() = default;

void RecentTabs::TabAdded(int id, bool active) {
  if (std::find(order_.begin(), order_.end(), id) != order_.end()) {
    return;
  }
  if (active) {
    order_.insert(order_.begin(), id);
  } else {
    order_.push_back(id);
  }
}

void RecentTabs::TabActivated(int id) {
  TabRemoved(id);
  order_.insert(order_.begin(), id);
}

void RecentTabs::TabRemoved(int id) {
  order_.erase(std::remove(order_.begin(), order_.end(), id), order_.end());
}

RecentTabSession::RecentTabSession() = default;
RecentTabSession::~RecentTabSession() = default;

bool RecentTabSession::Begin(const std::vector<int>& order,
                             bool backwards,
                             size_t max_visible) {
  End();
  if (order.size() < 2) {
    return false;
  }
  active_ = true;
  cards_ = order;
  selected_ = backwards ? cards_.size() - 1 : 1;
  max_visible_ = std::max<size_t>(max_visible, 1u);
  first_ = 0;
  KeepSelectionVisible();
  return true;
}

void RecentTabSession::End() {
  active_ = false;
  cards_.clear();
  selected_ = 0;
  first_ = 0;
}

void RecentTabSession::KeepSelectionVisible() {
  if (cards_.empty()) {
    first_ = 0;
    return;
  }
  if (selected_ < first_) {
    first_ = selected_;
  } else if (selected_ - first_ >= max_visible_) {
    first_ = selected_ + 1 - max_visible_;
  }
  // No empty room at the end while tabs are hidden at the start.
  if (cards_.size() > max_visible_) {
    first_ = std::min(first_, cards_.size() - max_visible_);
  } else {
    first_ = 0;
  }
}

std::vector<int> RecentTabSession::VisibleCards() const {
  if (!active_) {
    return {};
  }
  const size_t last = std::min(cards_.size(), first_ + max_visible_);
  return std::vector<int>(cards_.begin() + first_, cards_.begin() + last);
}

void RecentTabSession::StepForward() {
  if (active_) {
    selected_ = (selected_ + 1) % cards_.size();
    KeepSelectionVisible();
  }
}

void RecentTabSession::StepBackward() {
  if (active_) {
    selected_ = (selected_ + cards_.size() - 1) % cards_.size();
    KeepSelectionVisible();
  }
}

void RecentTabSession::TabRemoved(int id) {
  if (!active_) {
    return;
  }
  auto it = std::find(cards_.begin(), cards_.end(), id);
  if (it == cards_.end()) {
    return;
  }
  const size_t index = static_cast<size_t>(it - cards_.begin());
  cards_.erase(it);
  if (index < selected_) {
    --selected_;
  }
  if (selected_ >= cards_.size() && !cards_.empty()) {
    selected_ = cards_.size() - 1;
  }
  KeepSelectionVisible();
}

int RecentTabSession::selected() const {
  return active_ && selected_ < cards_.size() ? cards_[selected_] : -1;
}

}  // namespace aerie_sidebar
