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

bool RecentTabSession::Begin(const std::vector<int>& order, bool backwards) {
  End();
  if (order.size() < 2) {
    return false;
  }
  active_ = true;
  cards_ = order;
  selected_ = backwards ? cards_.size() - 1 : 1;
  return true;
}

void RecentTabSession::End() {
  active_ = false;
  cards_.clear();
  selected_ = 0;
}

void RecentTabSession::StepForward() {
  if (active_) {
    selected_ = (selected_ + 1) % cards_.size();
  }
}

void RecentTabSession::StepBackward() {
  if (active_) {
    selected_ = (selected_ + cards_.size() - 1) % cards_.size();
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
}

int RecentTabSession::selected() const {
  return active_ && selected_ < cards_.size() ? cards_[selected_] : -1;
}

}  // namespace aerie_sidebar
