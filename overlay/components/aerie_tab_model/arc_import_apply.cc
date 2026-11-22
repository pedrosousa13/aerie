// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_tab_model/arc_import_apply.h"

#include <algorithm>
#include <map>
#include <set>
#include <tuple>
#include <utility>
#include <vector>

#include "components/aerie_tab_model/folder_tree.h"
#include "components/aerie_tab_model/tab_model.h"
#include "components/aerie_tab_model/tab_model_types.h"
#include "components/aerie_tab_model/title_resolver.h"

namespace aerie_tab_model {

ArcImportCounts::ArcImportCounts() = default;
ArcImportCounts::ArcImportCounts(const ArcImportCounts&) = default;
ArcImportCounts& ArcImportCounts::operator=(const ArcImportCounts&) = default;
ArcImportCounts::~ArcImportCounts() = default;

namespace {

// The position that puts an item after every child of `parent` in `section`.
int NextPosition(const TabModel& model,
                 Section section,
                 const std::string& parent) {
  int next = 0;
  const std::string& workspace = model.workspace().id;
  for (const auto& [id, tab] : model.tabs()) {
    if (tab.workspace_id == workspace && tab.section == section &&
        tab.parent == parent) {
      next = std::max(next, tab.position + 1);
    }
  }
  for (const auto& [id, folder] : model.folders()) {
    if (folder.workspace_id == workspace && folder.section == section &&
        folder.parent == parent) {
      next = std::max(next, folder.position + 1);
    }
  }
  return next;
}

int CountPlanTabs(const std::vector<ArcPlanNode>& nodes) {
  int count = 0;
  for (const ArcPlanNode& node : nodes) {
    count += node.is_folder ? CountPlanTabs(node.children) : 1;
  }
  return count;
}

class Applier {
 public:
  Applier(TabModel& model, const ArcApplyOptions& options)
      : model_(model), options_(options) {
    int pinned = 0;
    for (const auto& [id, tab] : model.tabs()) {
      if (tab.workspace_id == model.workspace().id &&
          tab.section == Section::kPinned) {
        ++pinned;
      }
    }
    pinned_room_ = std::max(0, kArcMaxPinnedTabsPerWorkspace - pinned);
    // What is there already, to tell a second import of the same Space.
    for (const auto& [id, tab] : model.tabs()) {
      if (tab.workspace_id == model.workspace().id && tab.pinned_url &&
          (tab.section == Section::kPinned ||
           tab.section == Section::kFavorite)) {
        existing_tabs_.emplace(tab.section, tab.parent, *tab.pinned_url);
      }
    }
    for (const auto& [id, folder] : model.folders()) {
      if (folder.workspace_id == model.workspace().id &&
          folder.section == Section::kPinned) {
        existing_folders_.emplace(std::make_pair(folder.parent, folder.name),
                                  id);
      }
    }
  }

  // True, and counted, when a tab with this address was in the same place
  // before the import started (issue #276). Such a tab is not added, and it
  // uses up no room under any cap.
  bool SkipIfAlreadyThere(const ArcPlanTab& tab,
                          Section section,
                          const std::string& parent,
                          ArcImportCounts& counts) const {
    if (!existing_tabs_.contains(std::make_tuple(section, parent, tab.url))) {
      return false;
    }
    ++counts.skipped[ArcSkipReason::kAlreadyThere];
    return true;
  }

  bool AddNodes(const std::vector<ArcPlanNode>& nodes,
                const std::string& parent,
                int first_position,
                ArcImportCounts& counts) {
    int position = first_position;
    for (const ArcPlanNode& node : nodes) {
      if (node.is_folder) {
        // No folder after the cut, and none whose first tab does not fit.
        if (!FolderMayBeMade(node)) {
          counts.skipped[ArcSkipReason::kOverCap] += CountPlanTabs(node.children);
          continue;
        }
        std::optional<std::string> name =
            NormalizeFolderName(node.folder_name, std::string());
        const std::string folder_name =
            name ? *name : options_.untitled_folder_name;
        // The folder of an earlier import of the same Space: its tabs go in
        // there, and no second folder is made.
        auto same = existing_folders_.find(std::make_pair(parent, folder_name));
        if (same != existing_folders_.end()) {
          const std::string existing_id = same->second;
          if (!AddNodes(node.children, existing_id,
                        NextPosition(model_, Section::kPinned, existing_id),
                        counts)) {
            return false;
          }
          ++position;
          continue;
        }
        FolderRecord folder;
        folder.section = Section::kPinned;
        folder.parent = parent;
        folder.position = position++;
        folder.name = folder_name;
        if (!Allowed()) {
          return false;
        }
        const std::string id = model_.AddFolder(std::move(folder));
        if (id.empty()) {
          return false;
        }
        added_.push_back({true, id});
        ++counts.folders;
        if (!AddNodes(node.children, id, 0, counts)) {
          return false;
        }
      } else if (!AddTabWithinCaps(node.tab, Section::kPinned, parent, position++,
                                  counts)) {
        return false;
      }
    }
    return true;
  }

  // Adds the tab when the import is still under its caps. The first tab that
  // does not fit ends the room of its section: it and every tab after it, in
  // plan order, is counted as over the cap, so the import is a whole prefix.
  // The Pinned cap of the Workspace ends only Pinned; the cap of the import and
  // the text budget end everything.
  bool AddTabWithinCaps(const ArcPlanTab& tab,
                        Section section,
                        const std::string& parent,
                        int position,
                        ArcImportCounts& counts) {
    if (SkipIfAlreadyThere(tab, section, parent, counts)) {
      return true;
    }
    if (!Fits(tab, section)) {
      ++counts.skipped[ArcSkipReason::kOverCap];
      return true;
    }
    if (!AddTab(tab, section, parent, position, counts)) {
      return false;
    }
    ++imported_tabs_;
    text_bytes_ += TextBytes(tab);
    if (section == Section::kPinned) {
      --pinned_room_;
    }
    return true;
  }

  // Removes what was added, newest first, so a folder goes after its children.
  void Rollback() {
    for (auto it = added_.rbegin(); it != added_.rend(); ++it) {
      if (it->first) {
        model_.RemoveFolder(it->second);
      } else {
        model_.RemoveTab(it->second);
      }
    }
    added_.clear();
  }

 private:
  // Whether `tab` is still inside the caps. A tab that is not ends the room
  // of its section for good.
  bool Fits(const ArcPlanTab& tab, Section section) {
    if (import_closed_ || (section == Section::kPinned && pinned_closed_)) {
      return false;
    }
    if (imported_tabs_ >= kArcMaxTabsPerImport ||
        text_bytes_ + TextBytes(tab) > kArcMaxImportTextBytes) {
      import_closed_ = true;
    } else if (section == Section::kPinned && pinned_room_ <= 0) {
      pinned_closed_ = true;
    }
    return !import_closed_ && !(section == Section::kPinned && pinned_closed_);
  }

  // A folder is made unless the first tab in it is over the caps.
  bool FolderMayBeMade(const ArcPlanNode& folder) {
    const ArcPlanTab* first = FirstTab(folder.children);
    if (first) {
      return Fits(*first, Section::kPinned);
    }
    return !import_closed_ && !pinned_closed_;
  }

  static const ArcPlanTab* FirstTab(const std::vector<ArcPlanNode>& nodes) {
    for (const ArcPlanNode& node : nodes) {
      if (!node.is_folder) {
        return &node.tab;
      }
      if (const ArcPlanTab* tab = FirstTab(node.children)) {
        return tab;
      }
    }
    return nullptr;
  }

  bool AddTab(const ArcPlanTab& tab,
              Section section,
              const std::string& parent,
              int position,
              ArcImportCounts& counts) {
    TabRecord record;
    record.section = section;
    record.parent = parent;
    record.position = position;
    record.pinned_url = tab.url;
    if (!tab.last_title.empty()) {
      record.last_title = tab.last_title;
    }
    if (tab.custom_name) {
      record.manual_name = NormalizeManualName(*tab.custom_name, tab.last_title);
    }
    // Nothing is loaded: the row shows as asleep until the user selects it.
    record.sleeping = true;
    const bool named = record.manual_name.has_value();
    if (!Allowed()) {
      return false;
    }
    const std::string id = model_.AddTab(std::move(record));
    if (id.empty()) {
      return false;
    }
    added_.push_back({false, id});
    if (section == Section::kPinned) {
      ++counts.pinned_tabs;
    } else {
      ++counts.favorites;
    }
    counts.custom_names += named ? 1 : 0;
    return true;
  }

  bool Allowed() {
    const int index = attempts_++;
    return options_.fail_at_record_for_testing < 0 ||
           index != options_.fail_at_record_for_testing;
  }

  static size_t TextBytes(const ArcPlanTab& tab) {
    return tab.url.size() + tab.last_title.size() +
           (tab.custom_name ? tab.custom_name->size() : 0);
  }

  TabModel& model_;
  const ArcApplyOptions& options_;
  int attempts_ = 0;
  int pinned_room_ = 0;
  int imported_tabs_ = 0;
  size_t text_bytes_ = 0;
  bool import_closed_ = false;
  bool pinned_closed_ = false;
  // (section, parent folder id, address) of the tabs, and (parent, name) to the
  // id of the Pinned folders, as they were before the import.
  std::set<std::tuple<Section, std::string, std::string>> existing_tabs_;
  std::map<std::pair<std::string, std::string>, std::string> existing_folders_;
  // (is_folder, id), in the order they were added.
  std::vector<std::pair<bool, std::string>> added_;
};

}  // namespace

std::optional<ArcImportCounts> ApplyArcSpacePlan(TabModel& model,
                                                 const ArcSpacePlan& plan,
                                                 const ArcApplyOptions& options) {
  ArcImportCounts counts;
  counts.skipped = plan.skipped;
  Applier applier(model, options);

  if (!applier.AddNodes(plan.pinned, std::string(),
                        NextPosition(model, Section::kPinned, std::string()),
                        counts)) {
    applier.Rollback();
    return std::nullopt;
  }

  // The Favorites cap is for the Workspace: what is there counts.
  size_t have = 0;
  for (const auto& [id, tab] : model.tabs()) {
    if (tab.workspace_id == model.workspace().id &&
        tab.section == Section::kFavorite) {
      ++have;
    }
  }
  const size_t room = have >= kFavoritesCap ? 0 : kFavoritesCap - have;
  int position = NextPosition(model, Section::kFavorite, std::string());
  size_t taken = 0;
  for (const ArcPlanTab& tab : plan.favorites) {
    if (applier.SkipIfAlreadyThere(tab, Section::kFavorite, std::string(),
                                   counts)) {
      continue;
    }
    if (taken >= room) {
      ++counts.skipped[ArcSkipReason::kFavoritesFull];
      continue;
    }
    if (!applier.AddTabWithinCaps(tab, Section::kFavorite, std::string(), position++,
                                 counts)) {
      applier.Rollback();
      return std::nullopt;
    }
    ++taken;
  }
  return counts;
}

}  // namespace aerie_tab_model
