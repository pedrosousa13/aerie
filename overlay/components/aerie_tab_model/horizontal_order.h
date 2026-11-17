// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_TAB_MODEL_HORIZONTAL_ORDER_H_
#define COMPONENTS_AERIE_TAB_MODEL_HORIZONTAL_ORDER_H_

#include <string>
#include <vector>

#include "base/functional/function_ref.h"

// How the Tab Model's order maps onto Chromium's tab strip in the horizontal
// layouts (issue #186). The strip's order is the order on screen, so while the
// layout is horizontal the tabs in the TabStripModel are moved into the Tab
// Model's order, and a drag in the strip is written back as positions in the
// Tab Model. Plain logic over the model and over lists of record ids, so it has
// unit tests; the code that calls it in a window is in
// chrome/browser/aerie_tab_model/horizontal_order_mirror.*.
namespace aerie_tab_model {

class TabModel;

// The record ids of the tabs in the order the strip shows them: Favorites, then
// Pinned (depth first, with every folder flattened whether it is expanded or
// not), then Today (the same walk). A folder has no tab of its own here, and a
// smart folder's rows are not tabs. `tab_is_listed` turns away the tabs that
// have no tab in the window; an archived tab is never in the list.
std::vector<std::string> HorizontalStripOrder(
    const TabModel& model,
    base::FunctionRef<bool(const std::string&)> tab_is_listed);

// One move of a tab in the strip, as TabStripModel::MoveWebContentsAt takes it.
struct StripMove {
  int from = 0;
  int to = 0;

  bool operator==(const StripMove&) const = default;
};

// The moves that turn the strip `current` (the record id of each tab, in strip
// order; an empty string for a tab with no record) into the order `target`,
// applied one after the other. A tab that has no place in `target` goes after
// the others and keeps its order among them; an id of `target` that is not in
// the strip is skipped.
std::vector<StripMove> PlanStripMoves(const std::vector<std::string>& current,
                                      const std::vector<std::string>& target);

struct PositionChange {
  std::string id;
  int position = 0;

  bool operator==(const PositionChange&) const = default;
};

// What a reorder in the strip writes into the Tab Model. `strip_ids` is the
// strip's record ids in order. Only tabs of Pinned and Favorites are changed:
// Today follows the strip by itself (the tab strip adapter). Tabs that share a
// folder (or the section) trade the slots they hold, so that the slot of a
// folder among them stays; tabs of different folders or sections are never
// mixed, so a drag across groups changes nothing here (the mirror then puts the
// tab back).
std::vector<PositionChange> PositionsFromStripOrder(
    const TabModel& model,
    const std::vector<std::string>& strip_ids);

}  // namespace aerie_tab_model

#endif  // COMPONENTS_AERIE_TAB_MODEL_HORIZONTAL_ORDER_H_
