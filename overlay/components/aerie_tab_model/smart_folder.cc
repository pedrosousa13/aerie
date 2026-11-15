// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_tab_model/smart_folder.h"

#include <utility>

#include "components/aerie_tab_model/folder_rules.h"
#include "components/aerie_tab_model/tab_model.h"

namespace aerie_tab_model {

namespace {

constexpr std::string_view kSmartRowPrefix = "smart:";

const std::set<std::string>& NoMembers() {
  static const std::set<std::string> empty;
  return empty;
}

}  // namespace

// The folder id is escaped (`\\` for a backslash, `\:` for a colon) so that the
// first unescaped colon always ends it, whatever characters a loaded record id
// has. The tab id is the rest of the string and is not escaped.
std::string MakeSmartRowId(const std::string& folder_id,
                           const std::string& tab_id) {
  std::string id(kSmartRowPrefix);
  for (char c : folder_id) {
    if (c == '\\' || c == ':') {
      id.push_back('\\');
    }
    id.push_back(c);
  }
  id.push_back(':');
  id += tab_id;
  return id;
}

std::optional<SmartRowRef> ParseSmartRowId(std::string_view row_id) {
  if (!row_id.starts_with(kSmartRowPrefix)) {
    return std::nullopt;
  }
  row_id.remove_prefix(kSmartRowPrefix.size());
  std::string folder;
  size_t i = 0;
  for (; i < row_id.size(); ++i) {
    const char c = row_id[i];
    if (c == '\\') {
      if (i + 1 >= row_id.size()) {
        return std::nullopt;
      }
      folder.push_back(row_id[++i]);
    } else if (c == ':') {
      break;
    } else {
      folder.push_back(c);
    }
  }
  if (i >= row_id.size() || folder.empty() || i + 1 == row_id.size()) {
    return std::nullopt;
  }
  return SmartRowRef{std::move(folder), std::string(row_id.substr(i + 1))};
}

SmartFolderIndex::SmartFolderIndex() = default;
SmartFolderIndex::~SmartFolderIndex() = default;

const std::set<std::string>& SmartFolderIndex::Members(
    const std::string& folder_id) const {
  auto it = entries_.find(folder_id);
  return it == entries_.end() ? NoMembers() : it->second.members;
}

// static
bool SmartFolderIndex::IsListable(const TabRecord& tab, const Entry& entry) {
  return (tab.section == Section::kToday || tab.section == Section::kPinned) &&
         !tab.archived_at.has_value() && tab.workspace_id == entry.workspace_id;
}

// static
bool SmartFolderIndex::Matches(const Entry& entry,
                               const TabRecord& tab,
                               const TabFacts& facts) {
  return FindMatchingRule(entry.rules, facts.url, facts.title, tab.id) !=
         nullptr;
}

bool SmartFolderIndex::RefreshFolder(const TabModel& model,
                                     const std::string& folder_id,
                                     TabFactsFn facts) {
  const FolderRecord* folder = model.GetFolder(folder_id);
  auto existing = entries_.find(folder_id);
  if (!folder || folder->kind != FolderKind::kSmart) {
    if (existing == entries_.end()) {
      return false;
    }
    const bool had_members = !existing->second.members.empty();
    entries_.erase(existing);
    return had_members;
  }
  Entry entry;
  entry.rules = ReadFolderRules(*folder);
  entry.workspace_id = folder->workspace_id;
  // A change that is not about what the folder lists (it opened, was renamed
  // or moved) costs nothing: the rules, the switches and the workspace are what
  // decide the members, and tab changes reach the index through UpdateTab.
  last_scanned_ = 0;
  if (existing != entries_.end() &&
      existing->second.rules == entry.rules &&
      existing->second.workspace_id == entry.workspace_id) {
    return false;
  }
  for (const auto& [id, tab] : model.tabs()) {
    ++last_scanned_;
    if (!IsListable(tab, entry)) {
      continue;
    }
    std::optional<TabFacts> tab_facts = facts(id);
    if (tab_facts && Matches(entry, tab, *tab_facts)) {
      entry.members.insert(id);
    }
  }
  const bool changed =
      existing == entries_.end()
          ? !entry.members.empty()
          : existing->second.members != entry.members;
  entries_[folder_id] = std::move(entry);
  return changed;
}

void SmartFolderIndex::RemoveFolder(const std::string& folder_id) {
  entries_.erase(folder_id);
}

bool SmartFolderIndex::UpdateTab(const TabModel& model,
                                 const std::string& tab_id,
                                 std::optional<TabFacts> facts) {
  last_evaluations_ = 0;
  const TabRecord* tab = model.GetTab(tab_id);
  bool changed = false;
  for (auto& [folder_id, entry] : entries_) {
    ++last_evaluations_;
    const bool wanted = tab && facts && IsListable(*tab, entry) &&
                        Matches(entry, *tab, *facts);
    if (wanted) {
      changed |= entry.members.insert(tab_id).second;
    } else {
      changed |= entry.members.erase(tab_id) > 0;
    }
  }
  return changed;
}

bool SmartFolderIndex::RemoveTab(const std::string& tab_id) {
  bool changed = false;
  for (auto& [folder_id, entry] : entries_) {
    changed |= entry.members.erase(tab_id) > 0;
  }
  return changed;
}

std::vector<TreeRow> ExpandSmartFolders(
    const TabModel& model,
    const SmartFolderIndex& index,
    bool rules_enabled,
    Section section,
    std::vector<TreeRow> rows,
    base::FunctionRef<bool(const std::string&)> tab_is_listed) {
  if (!index.HasSmartFolders()) {
    return rows;
  }
  // The members in display order, built the first time a folder needs them:
  // the Pinned tabs of the tree, then the Today tabs.
  struct Ordered {
    std::string id;
    Section section;
  };
  std::optional<std::vector<Ordered>> order;
  auto ordered = [&]() -> const std::vector<Ordered>& {
    if (!order) {
      order.emplace();
      for (Section s : {Section::kPinned, Section::kToday}) {
        for (const TreeRow& row : BuildAllRows(model, s, tab_is_listed)) {
          if (!row.is_folder) {
            order->push_back({row.id, s});
          }
        }
      }
    }
    return *order;
  };

  std::vector<TreeRow> result;
  result.reserve(rows.size());
  for (TreeRow& row : rows) {
    const FolderRecord* folder =
        row.is_folder ? model.GetFolder(row.id) : nullptr;
    if (!folder || folder->kind != FolderKind::kSmart) {
      result.push_back(std::move(row));
      continue;
    }
    const std::set<std::string>& members = index.Members(row.id);
    const size_t count = rules_enabled ? members.size() : 0;
    row.item_count = static_cast<int>(count);
    const bool show = row.expanded && count > 0;
    const std::string folder_id = row.id;
    const int level = row.level;
    result.push_back(std::move(row));
    if (!show) {
      continue;
    }
    for (const Ordered& item : ordered()) {
      if (!members.contains(item.id)) {
        continue;
      }
      TreeRow member;
      member.id = MakeSmartRowId(folder_id, item.id);
      member.parent = folder_id;
      member.level = level + 1;
      member.smart_member = true;
      member.source_id = item.id;
      member.origin = item.section;
      result.push_back(std::move(member));
    }
  }
  return result;
}

}  // namespace aerie_tab_model
