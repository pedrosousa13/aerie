// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_tab_model/folder_tree.h"

#include <algorithm>
#include <map>
#include <utility>

#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "components/aerie_tab_model/tab_model.h"
#include "components/aerie_tab_model/title_resolver.h"

namespace aerie_tab_model {

namespace {

// More than any real tree: stops a walk through a loop in damaged data.
constexpr int kWalkBound = 64;

struct Child {
  bool is_folder = false;
  std::string id;
  int position = 0;
};

bool ChildLess(const Child& a, const Child& b) {
  if (a.position != b.position) {
    return a.position < b.position;
  }
  if (a.is_folder != b.is_folder) {
    return a.is_folder;
  }
  return a.id < b.id;
}

// The children of every parent of one section, each list in order. Archived
// tabs and tabs `tab_is_listed` turns away are left out when `listed_only`.
using ChildMap = std::map<std::string, std::vector<Child>>;

ChildMap BuildChildMap(const TabModel& model,
                       Section section,
                       bool listed_only,
                       base::FunctionRef<bool(const std::string&)> listed) {
  ChildMap children;
  const std::string& workspace = model.workspace().id;
  for (const auto& [id, folder] : model.folders()) {
    if (folder.workspace_id == workspace && folder.section == section) {
      children[folder.parent].push_back({true, id, folder.position});
    }
  }
  for (const auto& [id, tab] : model.tabs()) {
    if (tab.workspace_id != workspace || tab.section != section) {
      continue;
    }
    if (listed_only && (tab.archived_at.has_value() || !listed(id))) {
      continue;
    }
    children[tab.parent].push_back({false, id, tab.position});
  }
  for (auto& [parent, list] : children) {
    std::sort(list.begin(), list.end(), ChildLess);
  }
  return children;
}

void Walk(const TabModel& model,
          const ChildMap& children,
          const std::string& parent,
          int level,
          bool only_expanded,
          std::vector<TreeRow>& out) {
  auto it = children.find(parent);
  if (it == children.end() || level > kWalkBound) {
    return;
  }
  for (const Child& child : it->second) {
    TreeRow row;
    row.is_folder = child.is_folder;
    row.id = child.id;
    row.parent = parent;
    row.level = level;
    if (child.is_folder) {
      const FolderRecord* folder = model.GetFolder(child.id);
      row.expanded = folder->expanded;
      auto own = children.find(child.id);
      row.item_count = own == children.end() ? 0 : static_cast<int>(own->second.size());
      out.push_back(row);
      if (!only_expanded || folder->expanded) {
        Walk(model, children, child.id, level + 1, only_expanded, out);
      }
    } else {
      out.push_back(row);
    }
  }
}

std::vector<TreeRow> BuildRows(
    const TabModel& model,
    Section section,
    bool only_expanded,
    base::FunctionRef<bool(const std::string&)> listed) {
  const ChildMap children = BuildChildMap(model, section, true, listed);
  std::vector<TreeRow> rows;
  Walk(model, children, std::string(), 0, only_expanded, rows);
  return rows;
}

// Every child, archived ones too, of one parent, in order.
std::vector<Child> AllChildren(const TabModel& model,
                               Section section,
                               const std::string& parent) {
  ChildMap children = BuildChildMap(model, section, false,
                                    [](const std::string&) { return true; });
  auto it = children.find(parent);
  return it == children.end() ? std::vector<Child>() : std::move(it->second);
}

// Moves a tab or a folder inside its parent, to `position`.
void Reposition(TabModel& model, const Child& child, int position) {
  if (child.is_folder) {
    const FolderRecord* folder = model.GetFolder(child.id);
    if (folder && folder->position != position) {
      model.MoveFolder(child.id, folder->section, folder->parent, position);
    }
  } else {
    const TabRecord* tab = model.GetTab(child.id);
    if (tab && tab->position != position) {
      model.MoveTab(child.id, tab->section, tab->parent, position);
    }
  }
}

int TopPosition(const TabModel& model, Section section,
                const std::string& parent) {
  const std::vector<Child> children = AllChildren(model, section, parent);
  return children.empty() ? 0 : children.front().position - 1;
}

int EndPosition(const TabModel& model, Section section,
                const std::string& parent) {
  const std::vector<Child> children = AllChildren(model, section, parent);
  return children.empty() ? 0 : children.back().position + 1;
}

}  // namespace

int Depth(const TabModel& model, const std::string& folder_id) {
  int depth = 0;
  const FolderRecord* folder = model.GetFolder(folder_id);
  while (folder && depth <= kWalkBound) {
    ++depth;
    folder = folder->parent.empty() ? nullptr : model.GetFolder(folder->parent);
  }
  return depth;
}

namespace {
int HeightBelow(const TabModel& model, const std::string& id, int budget) {
  if (budget <= 0) {
    return 0;
  }
  int tallest = 0;
  for (const auto& [child_id, folder] : model.folders()) {
    if (folder.parent == id) {
      tallest = std::max(tallest, HeightBelow(model, child_id, budget - 1));
    }
  }
  return tallest + 1;
}
}  // namespace

int SubtreeHeight(const TabModel& model, const std::string& folder_id) {
  if (!model.GetFolder(folder_id)) {
    return 0;
  }
  return HeightBelow(model, folder_id, kWalkBound);
}

bool CanNest(const TabModel& model,
             ItemKind kind,
             const std::string& item_id,
             const std::string& target_folder_id) {
  if (!target_folder_id.empty() && !model.GetFolder(target_folder_id)) {
    return false;
  }
  if (kind == ItemKind::kTab) {
    return model.GetTab(item_id) &&
           Depth(model, target_folder_id) <= kMaxFolderDepth;
  }
  if (!model.GetFolder(item_id)) {
    return false;
  }
  // Into itself, or into something below itself.
  const FolderRecord* walk = model.GetFolder(target_folder_id);
  for (int steps = 0; walk && steps <= kWalkBound; ++steps) {
    if (walk->id == item_id) {
      return false;
    }
    walk = walk->parent.empty() ? nullptr : model.GetFolder(walk->parent);
  }
  return Depth(model, target_folder_id) + SubtreeHeight(model, item_id) <=
         kMaxFolderDepth;
}

bool CanCreateFolderIn(const TabModel& model,
                       const std::string& parent_folder_id) {
  if (!parent_folder_id.empty() && !model.GetFolder(parent_folder_id)) {
    return false;
  }
  return Depth(model, parent_folder_id) + 1 <= kMaxFolderDepth;
}

std::vector<TreeRow> BuildVisibleRows(
    const TabModel& model,
    Section section,
    base::FunctionRef<bool(const std::string&)> tab_is_listed) {
  return BuildRows(model, section, /*only_expanded=*/true, tab_is_listed);
}

std::vector<TreeRow> BuildAllRows(
    const TabModel& model,
    Section section,
    base::FunctionRef<bool(const std::string&)> tab_is_listed) {
  return BuildRows(model, section, /*only_expanded=*/false, tab_is_listed);
}

bool FolderContainsTab(const TabModel& model,
                       const std::string& folder_id,
                       const std::string& tab_id) {
  if (!model.GetFolder(folder_id)) {
    return false;
  }
  const TabRecord* tab = model.GetTab(tab_id);
  if (!tab) {
    return false;
  }
  const FolderRecord* walk = model.GetFolder(tab->parent);
  for (int steps = 0; walk && steps <= kWalkBound; ++steps) {
    if (walk->id == folder_id) {
      return true;
    }
    walk = walk->parent.empty() ? nullptr : model.GetFolder(walk->parent);
  }
  return false;
}

std::vector<FolderChoice> FolderChoices(const TabModel& model,
                                        Section section) {
  std::vector<FolderChoice> choices;
  for (const TreeRow& row : BuildAllRows(
           model, section, [](const std::string&) { return false; })) {
    if (row.is_folder) {
      choices.push_back({row.id, row.level + 1, model.GetFolder(row.id)->name});
    }
  }
  return choices;
}

std::string CreateFolder(TabModel& model,
                         Section section,
                         const std::string& parent_folder_id,
                         const std::string& name) {
  if (section == Section::kFavorite || !CanCreateFolderIn(model, parent_folder_id)) {
    return std::string();
  }
  FolderRecord folder;
  folder.section = section;
  folder.parent = parent_folder_id;
  folder.position = TopPosition(model, section, parent_folder_id);
  folder.name = name;
  folder.kind = FolderKind::kManual;
  folder.expanded = true;
  return model.AddFolder(std::move(folder));
}

bool SetFolderExpanded(TabModel& model, const std::string& id, bool expanded) {
  const FolderRecord* folder = model.GetFolder(id);
  if (!folder) {
    return false;
  }
  if (folder->expanded == expanded) {
    return true;
  }
  return model.UpdateFolder(id, [expanded](FolderRecord& f) { f.expanded = expanded; });
}

std::optional<std::string> NormalizeFolderName(std::string_view input,
                                               std::string_view old_name) {
  std::u16string text;
  base::TrimWhitespace(base::UTF8ToUTF16(input), base::TRIM_ALL, &text);
  text = CutToManualNameLength(text);
  base::TrimWhitespace(text, base::TRIM_ALL, &text);
  if (text.empty()) {
    return std::nullopt;
  }
  std::string utf8 = base::UTF16ToUTF8(text);
  if (utf8 == old_name) {
    return std::nullopt;
  }
  return utf8;
}

bool RenameFolder(TabModel& model, const std::string& id, std::string_view input) {
  const FolderRecord* folder = model.GetFolder(id);
  if (!folder) {
    return false;
  }
  std::optional<std::string> name = NormalizeFolderName(input, folder->name);
  if (!name) {
    return false;
  }
  return model.UpdateFolder(id, [&](FolderRecord& f) { f.name = *name; });
}

bool FolderIsEmpty(const TabModel& model, const std::string& id) {
  for (const auto& [tab_id, tab] : model.tabs()) {
    if (tab.parent == id) {
      return false;
    }
  }
  for (const auto& [folder_id, folder] : model.folders()) {
    if (folder.parent == id) {
      return false;
    }
  }
  return true;
}

bool DeleteFolder(TabModel& model, const std::string& id) {
  const FolderRecord* folder = model.GetFolder(id);
  if (!folder) {
    return false;
  }
  const Section section = folder->section;
  const std::string parent = folder->parent;
  const int slot = folder->position;
  const std::vector<Child> inside = AllChildren(model, section, id);
  const int count = static_cast<int>(inside.size());
  if (count > 0) {
    // The siblings after the folder make room, from the last one up.
    const std::vector<Child> siblings = AllChildren(model, section, parent);
    Child self{true, id, slot};
    for (auto it = siblings.rbegin(); it != siblings.rend(); ++it) {
      if (it->id != id && ChildLess(self, *it)) {
        Reposition(model, *it, it->position + count);
      }
    }
    for (int i = 0; i < count; ++i) {
      const Child& child = inside[i];
      if (child.is_folder) {
        model.MoveFolder(child.id, section, parent, slot + i);
      } else {
        model.MoveTab(child.id, section, parent, slot + i);
      }
    }
  }
  return model.RemoveFolder(id);
}

bool MoveTabToFolder(TabModel& model,
                     const std::string& tab_id,
                     const std::string& folder_id) {
  const TabRecord* tab = model.GetTab(tab_id);
  if (!tab || tab->section == Section::kFavorite) {
    return false;
  }
  const Section section = tab->section;
  if (folder_id.empty()) {
    // Leave the folder the tab is in, for the one above it.
    const FolderRecord* current = model.GetFolder(tab->parent);
    if (!current) {
      return false;
    }
    return model.MoveTab(tab_id, section, current->parent, current->position);
  }
  const FolderRecord* target = model.GetFolder(folder_id);
  if (!target || target->section != section ||
      !CanNest(model, ItemKind::kTab, tab_id, folder_id)) {
    return false;
  }
  return model.MoveTab(tab_id, section, folder_id,
                       EndPosition(model, section, folder_id));
}

std::string MoveTabToNewFolder(TabModel& model,
                               const std::string& tab_id,
                               const std::string& name) {
  const TabRecord* tab = model.GetTab(tab_id);
  if (!tab || tab->section == Section::kFavorite ||
      !CanCreateFolderIn(model, tab->parent) ||
      !CanNest(model, ItemKind::kTab, tab_id, tab->parent)) {
    return std::string();
  }
  FolderRecord folder;
  folder.section = tab->section;
  folder.parent = tab->parent;
  // A folder sorts before a tab at the same position: it takes the tab's place.
  folder.position = tab->position;
  folder.name = name;
  folder.expanded = true;
  const Section section = tab->section;
  const std::string folder_id = model.AddFolder(std::move(folder));
  if (folder_id.empty()) {
    return folder_id;
  }
  model.MoveTab(tab_id, section, folder_id, 0);
  return folder_id;
}

}  // namespace aerie_tab_model
