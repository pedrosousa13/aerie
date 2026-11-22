// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_SIDEBAR_AERIE_SIDEBAR_RECENT_TABS_H_
#define COMPONENTS_AERIE_SIDEBAR_AERIE_SIDEBAR_RECENT_TABS_H_

#include <cstddef>
#include <vector>

// Which tab was used when, and the order that Ctrl+Tab walks (issue #269).
// Headless: the view tells it which tab became active, which was added and
// which closed, and shows the cards of a session in the order it keeps.
//
// A tab is an opaque id (the view uses the raw value of the tab handle).
//
// Scope: the tabs of one browser window. That stands in for "the current
// Workspace" until Workspaces exist (M5): when a window shows one Workspace at
// a time, the view hands this model only the tabs of the open Workspace, and
// nothing in here changes.
namespace aerie_sidebar {

// The tabs of one window, the most recently used first.
class RecentTabs {
 public:
  RecentTabs();
  RecentTabs(const RecentTabs&);
  RecentTabs& operator=(const RecentTabs&);
  ~RecentTabs();

  // A tab came into the window. The active one goes in front; any other (a
  // background tab, or a tab of a restored window) goes behind everything,
  // so a tab nobody has used yet is the least recent. A known id is ignored.
  void TabAdded(int id, bool active);
  // The tab became the active one: it moves to the front. An unknown tab is
  // added in front.
  void TabActivated(int id);
  // The tab closed. An unknown id is ignored.
  void TabRemoved(int id);

  // The tabs, most recent first.
  const std::vector<int>& Order() const { return order_; }

 private:
  std::vector<int> order_;
};

// One open Ctrl+Tab switcher: a snapshot of the order, and the selected card.
// The first Ctrl+Tab selects the previous tab (the second card). Each further
// Tab steps along and wraps. Ctrl+Shift+Tab steps back; as the first press it
// selects the least recent tab, as every browser does. Releasing Ctrl switches
// to `selected()`; Esc ends the session without switching.
class RecentTabSession {
 public:
  RecentTabSession();
  ~RecentTabSession();

  // Starts a session over `order` (most recent first). Returns false and stays
  // inactive when there are fewer than two tabs: there is nothing to switch to.
  bool Begin(const std::vector<int>& order, bool backwards);
  void End();
  bool active() const { return active_; }

  void StepForward();
  void StepBackward();

  // A tab closed while the switcher is open: its card goes. The selection
  // stays on the same tab; when that tab is the one that closed, the card
  // after it (or the new last one) is selected.
  void TabRemoved(int id);

  const std::vector<int>& cards() const { return cards_; }
  size_t selected_index() const { return selected_; }
  // The selected tab, or -1 when the session is not active.
  int selected() const;

 private:
  bool active_ = false;
  std::vector<int> cards_;
  size_t selected_ = 0;
};

}  // namespace aerie_sidebar

#endif  // COMPONENTS_AERIE_SIDEBAR_AERIE_SIDEBAR_RECENT_TABS_H_
