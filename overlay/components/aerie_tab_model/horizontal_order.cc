// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_tab_model/horizontal_order.h"

#include <algorithm>
#include <map>
#include <set>
#include <utility>

#include "components/aerie_tab_model/folder_tree.h"
#include "components/aerie_tab_model/tab_model.h"

namespace aerie_tab_model {

std::vector<std::string> HorizontalStripOrder(
    const TabModel& model,
    base::FunctionRef<bool(const std::string&)> tab_is_listed) {
  std::vector<std::string> order;
  for (Section section :
       {Section::kFavorite, Section::kPinned, Section::kToday}) {
    for (const TreeRow& row : BuildAllRows(model, section, tab_is_listed)) {
      if (!row.is_folder && !row.smart_member) {
        order.push_back(row.id);
      }
    }
  }
  return order;
}

std::vector<StripMove> PlanStripMoves(const std::vector<std::string>& current,
                                      const std::vector<std::string>& target) {
  // The wanted order of the strip: the target ids the strip has, then the
  // other tabs in the order they have.
  std::set<std::string> in_strip;
  for (const std::string& id : current) {
    if (!id.empty()) {
      in_strip.insert(id);
    }
  }
  std::vector<std::string> wanted;
  std::set<std::string> placed;
  for (const std::string& id : target) {
    if (in_strip.contains(id) && placed.insert(id).second) {
      wanted.push_back(id);
    }
  }
  // A tab with no record cannot be named; it is tracked by a slot number that
  // no real id has.
  std::vector<std::string> working = current;
  for (size_t i = 0; i < working.size(); ++i) {
    if (working[i].empty() || !placed.contains(working[i])) {
      if (working[i].empty()) {
        working[i] = "\x01unnamed-" + std::to_string(i);
      }
      wanted.push_back(working[i]);
    }
  }
  std::vector<StripMove> moves;
  for (size_t to = 0; to < wanted.size(); ++to) {
    auto it = std::find(working.begin() + to, working.end(), wanted[to]);
    if (it == working.end()) {
      continue;
    }
    const size_t from = it - working.begin();
    if (from == to) {
      continue;
    }
    std::string id = std::move(*it);
    working.erase(it);
    working.insert(working.begin() + to, std::move(id));
    moves.push_back({static_cast<int>(from), static_cast<int>(to)});
  }
  return moves;
}

std::vector<PositionChange> PositionsFromStripOrder(
    const TabModel& model,
    const std::vector<std::string>& strip_ids) {
  // Tabs of the strip by group (section and parent folder), in strip order.
  using Group = std::pair<Section, std::string>;
  std::map<Group, std::vector<const TabRecord*>> by_group;
  for (const std::string& id : strip_ids) {
    const TabRecord* tab = model.GetTab(id);
    if (!tab || tab->section == Section::kToday) {
      continue;
    }
    by_group[{tab->section, tab->parent}].push_back(tab);
  }
  std::vector<PositionChange> changes;
  for (auto& [group, in_strip_order] : by_group) {
    // The slots those tabs hold now, lowest first; the tab that comes first in
    // the strip takes the lowest.
    std::vector<int> slots;
    for (const TabRecord* tab : in_strip_order) {
      slots.push_back(tab->position);
    }
    std::sort(slots.begin(), slots.end());
    for (size_t i = 0; i < in_strip_order.size(); ++i) {
      if (in_strip_order[i]->position != slots[i]) {
        changes.push_back({in_strip_order[i]->id, slots[i]});
      }
    }
  }
  return changes;
}

}  // namespace aerie_tab_model
