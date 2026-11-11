// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_tab_model/pinned_tabs.h"

#include <algorithm>

#include "components/aerie_tab_model/tab_model.h"
#include "url/gurl.h"

namespace aerie_tab_model {

namespace {

// The position that puts a tab after (`at_end`) or before every tab of
// `section` in the Workspace.
int EdgePosition(const TabModel& model, Section section, bool at_end) {
  bool any = false;
  int edge = 0;
  for (const auto& [id, tab] : model.tabs()) {
    if (tab.section != section || tab.workspace_id != model.workspace().id) {
      continue;
    }
    if (!any) {
      edge = tab.position;
      any = true;
    } else {
      edge = at_end ? std::max(edge, tab.position)
                    : std::min(edge, tab.position);
    }
  }
  if (!any) {
    return 0;
  }
  return at_end ? edge + 1 : edge - 1;
}

}  // namespace

std::vector<std::string> SectionTabIds(const TabModel& model, Section section) {
  std::vector<const TabRecord*> tabs;
  for (const auto& [id, tab] : model.tabs()) {
    if (tab.workspace_id == model.workspace().id && tab.section == section &&
        tab.parent.empty() && !tab.archived_at.has_value()) {
      tabs.push_back(&tab);
    }
  }
  std::sort(tabs.begin(), tabs.end(), [](const auto* a, const auto* b) {
    return a->position != b->position ? a->position < b->position
                                      : a->id < b->id;
  });
  std::vector<std::string> ids;
  for (const TabRecord* tab : tabs) {
    ids.push_back(tab->id);
  }
  return ids;
}

std::vector<std::string> SidebarTabOrder(const TabModel& model) {
  std::vector<std::string> order = SectionTabIds(model, Section::kFavorite);
  for (Section section : {Section::kPinned, Section::kToday}) {
    std::vector<std::string> ids = SectionTabIds(model, section);
    order.insert(order.end(), ids.begin(), ids.end());
  }
  return order;
}

std::string PickNextTab(const std::vector<std::string>& order,
                        const std::string& id) {
  auto it = std::find(order.begin(), order.end(), id);
  if (it == order.end()) {
    return std::string();
  }
  if (it + 1 != order.end()) {
    return *(it + 1);
  }
  if (it != order.begin()) {
    return *(it - 1);
  }
  return std::string();
}

bool PinTab(TabModel& model, const std::string& id, const std::string& url) {
  const TabRecord* tab = model.GetTab(id);
  if (!tab) {
    return false;
  }
  // The move first: if it fails, the record is left as it was.
  if (!model.MoveTab(id, Section::kPinned, std::string(),
                     EdgePosition(model, Section::kPinned, /*at_end=*/true))) {
    return false;
  }
  model.UpdateTab(id, [&url](TabRecord& record) { record.pinned_url = url; });
  return true;
}

bool UnpinTab(TabModel& model, const std::string& id) {
  if (!model.GetTab(id)) {
    return false;
  }
  if (!model.MoveTab(id, Section::kToday, std::string(),
                     EdgePosition(model, Section::kToday, /*at_end=*/false))) {
    return false;
  }
  model.UpdateTab(id, [](TabRecord& record) { record.pinned_url.reset(); });
  return true;
}

FavoriteResult AddToFavorites(TabModel& model,
                              const std::string& id,
                              const std::string& url) {
  const TabRecord* tab = model.GetTab(id);
  if (!tab) {
    return FavoriteResult::kUnknownTab;
  }
  const bool had_url = tab->pinned_url.has_value();
  // MoveTab refuses the 13th Favorite and changes nothing, so the move comes
  // first and the pinned URL only follows a move that worked.
  if (!model.MoveTab(id, Section::kFavorite, std::string(),
                     EdgePosition(model, Section::kFavorite, /*at_end=*/true))) {
    return FavoriteResult::kFull;
  }
  if (!had_url) {
    model.UpdateTab(id, [&url](TabRecord& record) { record.pinned_url = url; });
  }
  return FavoriteResult::kAdded;
}

bool RemoveFromFavorites(TabModel& model, const std::string& id) {
  const TabRecord* tab = model.GetTab(id);
  if (!tab || tab->section != Section::kFavorite) {
    return false;
  }
  const bool back_to_pinned = tab->previous_section == Section::kPinned;
  if (!back_to_pinned) {
    model.UpdateTab(id, [](TabRecord& record) { record.pinned_url.reset(); });
  }
  const Section target = back_to_pinned ? Section::kPinned : Section::kToday;
  return model.MoveTab(
      id, target, std::string(),
      EdgePosition(model, target, /*at_end=*/back_to_pinned));
}

bool IsAtPinnedUrl(const GURL& current, const GURL& pinned) {
  if (current.is_empty() || !pinned.is_valid()) {
    return true;
  }
  return current.scheme() == pinned.scheme() &&
         current.host() == pinned.host() &&
         current.EffectiveIntPort() == pinned.EffectiveIntPort() &&
         current.path() == pinned.path();
}

PinnedCloseMode ParsePinnedCloseMode(std::string_view value) {
  if (value == "sleep") {
    return PinnedCloseMode::kSleep;
  }
  if (value == "close") {
    return PinnedCloseMode::kClose;
  }
  return PinnedCloseMode::kResetSleepSwitch;
}

PinnedCloseAction DecidePinnedClose(PinnedCloseMode mode,
                                    bool asleep,
                                    bool at_pinned_url) {
  switch (mode) {
    case PinnedCloseMode::kResetSleepSwitch:
      if (!at_pinned_url) {
        return PinnedCloseAction::kResetSleepSwitch;
      }
      // At the pinned page there is nothing to reset: sleep, and the next press
      // closes.
      return asleep ? PinnedCloseAction::kUnpinClose
                    : PinnedCloseAction::kSleepSwitch;
    case PinnedCloseMode::kSleep:
      return asleep ? PinnedCloseAction::kUnpinClose
                    : PinnedCloseAction::kSleepSwitch;
    case PinnedCloseMode::kClose:
      return PinnedCloseAction::kUnpinClose;
  }
}

}  // namespace aerie_tab_model
