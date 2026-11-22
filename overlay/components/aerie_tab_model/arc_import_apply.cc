// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_tab_model/arc_import_apply.h"

#include <algorithm>
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
    pinned_room_ = std::max(0, kMaxPinnedTabsPerWorkspace - pinned);
  }

  bool AddNodes(const std::vector<ArcPlanNode>& nodes,
                const std::string& parent,
                int first_position,
                ArcImportCounts& counts) {
    int position = first_position;
    for (const ArcPlanNode& node : nodes) {
      if (node.is_folder) {
        if (over_cap_) {
          counts.skipped[ArcSkipReason::kOverCap] += CountPlanTabs(node.children);
          continue;
        }
        FolderRecord folder;
        folder.section = Section::kPinned;
        folder.parent = parent;
        folder.position = position++;
        std::optional<std::string> name =
            NormalizeFolderName(node.folder_name, std::string());
        folder.name = name ? *name : options_.untitled_folder_name;
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
      } else if (!TakeTab(node.tab, Section::kPinned, parent, position++,
                          counts)) {
        return false;
      }
    }
    return true;
  }

  // Adds the tab when the import is still under its caps. The first tab that
  // does not fit ends the import's room: it and every tab after it, in plan
  // order, is counted as over the cap, so the import is a whole prefix.
  bool TakeTab(const ArcPlanTab& tab,
               Section section,
               const std::string& parent,
               int position,
               ArcImportCounts& counts) {
    const size_t bytes = TextBytes(tab);
    if (!over_cap_ &&
        (imported_tabs_ >= kArcMaxTabsPerImport ||
         (section == Section::kPinned && pinned_room_ <= 0) ||
         text_bytes_ + bytes > kArcMaxImportTextBytes)) {
      over_cap_ = true;
    }
    if (over_cap_) {
      ++counts.skipped[ArcSkipReason::kOverCap];
      return true;
    }
    if (!AddTab(tab, section, parent, position, counts)) {
      return false;
    }
    ++imported_tabs_;
    text_bytes_ += bytes;
    if (section == Section::kPinned) {
      --pinned_room_;
    }
    return true;
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
  bool over_cap_ = false;
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
    if (taken >= room) {
      ++counts.skipped[ArcSkipReason::kFavoritesFull];
      continue;
    }
    if (!applier.TakeTab(tab, Section::kFavorite, std::string(), position++,
                         counts)) {
      applier.Rollback();
      return std::nullopt;
    }
    ++taken;
  }
  return counts;
}

}  // namespace aerie_tab_model
