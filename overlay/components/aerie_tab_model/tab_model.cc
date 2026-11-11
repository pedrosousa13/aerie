// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_tab_model/tab_model.h"

#include <utility>
#include <vector>

#include "base/check.h"
#include "base/json/values_util.h"
#include "base/time/clock.h"
#include "base/uuid.h"

namespace aerie_tab_model {

TabModel::TabModel(const base::Clock* clock,
                   IdGenerator id_generator,
                   size_t max_change_log_entries)
    : clock_(clock),
      id_generator_(std::move(id_generator)),
      max_change_log_entries_(max_change_log_entries) {
  CHECK(clock_);
  CHECK_GT(max_change_log_entries_, 0u);
  // The Workspace of a fresh profile. Its creation is logged when the read of
  // state.json finds nothing (MergeLoaded), so a stored one is not logged twice.
  base::Time now = clock_->Now();
  workspace_.id = kDefaultWorkspaceId;
  workspace_.created = now;
  workspace_.modified = now;
  workspace_.name = "Personal";
  workspace_.icon = "ring";
  workspace_.colour = "dusk";
  workspace_.theme = "default";
  workspace_.mode = "default";
}

TabModel::~TabModel() = default;

void TabModel::AddObserver(TabModelObserver* observer) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  observers_.AddObserver(observer);
}

void TabModel::RemoveObserver(TabModelObserver* observer) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  observers_.RemoveObserver(observer);
}

std::string TabModel::NewId() const {
  if (id_generator_) {
    return id_generator_.Run();
  }
  return base::Uuid::GenerateRandomV4().AsLowercaseString();
}

void TabModel::AppendChange(ChangeType type,
                            RecordKind kind,
                            const std::string& record_id,
                            base::Time now) {
  ChangeRecord change;
  change.counter = next_change_++;
  change.id = NewId();
  change.type = type;
  change.kind = kind;
  change.record_id = record_id;
  change.time = now;
  change_log_.push_back(std::move(change));
  while (change_log_.size() > max_change_log_entries_) {
    change_log_.pop_front();
  }
}

void TabModel::Stamp(Record& record, ChangeType type, RecordKind kind) {
  base::Time now = clock_->Now();
  record.modified = now;
  record.change = next_change_;
  AppendChange(type, kind, record.id, now);
}

void TabModel::NotifyChanged() {
  observers_.Notify(&TabModelObserver::OnModelChanged);
}

const TabRecord* TabModel::GetTab(const std::string& id) const {
  auto it = tabs_.find(id);
  return it == tabs_.end() ? nullptr : &it->second;
}

const FolderRecord* TabModel::GetFolder(const std::string& id) const {
  auto it = folders_.find(id);
  return it == folders_.end() ? nullptr : &it->second;
}

size_t TabModel::CountFavorites(const std::string& workspace_id,
                                const std::string& except_id) const {
  size_t count = 0;
  for (const auto& [id, tab] : tabs_) {
    if (tab.section == Section::kFavorite && tab.workspace_id == workspace_id &&
        id != except_id) {
      ++count;
    }
  }
  return count;
}

bool TabModel::PlacementIsValid(const std::string& workspace_id,
                                const std::string& self_folder_id,
                                const std::string& parent) const {
  if (parent.empty()) {
    return true;
  }
  // Walk up from the new parent. Reaching the folder itself is a cycle. The
  // bound stops a loop in damaged data.
  const FolderRecord* folder = GetFolder(parent);
  if (!folder || folder->workspace_id != workspace_id) {
    return false;
  }
  for (size_t steps = 0; folder && steps <= folders_.size(); ++steps) {
    if (!self_folder_id.empty() && folder->id == self_folder_id) {
      return false;
    }
    folder = folder->parent.empty() ? nullptr : GetFolder(folder->parent);
  }
  return true;
}

void TabModel::UpdateWorkspace(base::FunctionRef<void(Workspace&)> mutate) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const std::string id = workspace_.id;
  const base::Time created = workspace_.created;
  mutate(workspace_);
  workspace_.id = id;
  workspace_.created = created;
  Stamp(workspace_, ChangeType::kChanged, RecordKind::kWorkspace);
  observers_.Notify(&TabModelObserver::OnWorkspaceChanged, workspace_);
  NotifyChanged();
}

std::string TabModel::AddTab(TabRecord tab) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (tab.workspace_id.empty()) {
    tab.workspace_id = workspace_.id;
  }
  if (!PlacementIsValid(tab.workspace_id, std::string(), tab.parent)) {
    return std::string();
  }
  if (tab.section == Section::kFavorite &&
      CountFavorites(tab.workspace_id, std::string()) >= kFavoritesCap) {
    return std::string();
  }
  tab.id = NewId();
  tab.created = clock_->Now();
  Stamp(tab, ChangeType::kAdded, RecordKind::kTab);
  const std::string id = tab.id;
  auto [it, inserted] = tabs_.emplace(id, std::move(tab));
  CHECK(inserted);
  observers_.Notify(&TabModelObserver::OnTabAdded, it->second);
  NotifyChanged();
  return id;
}

std::string TabModel::AddFolder(FolderRecord folder) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (folder.workspace_id.empty()) {
    folder.workspace_id = workspace_.id;
  }
  if (!PlacementIsValid(folder.workspace_id, std::string(), folder.parent)) {
    return std::string();
  }
  folder.id = NewId();
  folder.created = clock_->Now();
  Stamp(folder, ChangeType::kAdded, RecordKind::kFolder);
  const std::string id = folder.id;
  auto [it, inserted] = folders_.emplace(id, std::move(folder));
  CHECK(inserted);
  observers_.Notify(&TabModelObserver::OnFolderAdded, it->second);
  NotifyChanged();
  return id;
}

bool TabModel::UpdateTab(const std::string& id,
                         base::FunctionRef<void(TabRecord&)> mutate) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto it = tabs_.find(id);
  if (it == tabs_.end()) {
    return false;
  }
  TabRecord& tab = it->second;
  const base::Time created = tab.created;
  const std::string workspace_id = tab.workspace_id;
  const Section section = tab.section;
  const std::string parent = tab.parent;
  const int position = tab.position;
  mutate(tab);
  tab.id = id;
  tab.created = created;
  tab.workspace_id = workspace_id;
  tab.section = section;
  tab.parent = parent;
  tab.position = position;
  Stamp(tab, ChangeType::kChanged, RecordKind::kTab);
  observers_.Notify(&TabModelObserver::OnTabChanged, tab);
  NotifyChanged();
  return true;
}

bool TabModel::UpdateFolder(const std::string& id,
                            base::FunctionRef<void(FolderRecord&)> mutate) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto it = folders_.find(id);
  if (it == folders_.end()) {
    return false;
  }
  FolderRecord& folder = it->second;
  const base::Time created = folder.created;
  const std::string workspace_id = folder.workspace_id;
  const Section section = folder.section;
  const std::string parent = folder.parent;
  const int position = folder.position;
  mutate(folder);
  folder.id = id;
  folder.created = created;
  folder.workspace_id = workspace_id;
  folder.section = section;
  folder.parent = parent;
  folder.position = position;
  Stamp(folder, ChangeType::kChanged, RecordKind::kFolder);
  observers_.Notify(&TabModelObserver::OnFolderChanged, folder);
  NotifyChanged();
  return true;
}

bool TabModel::MoveTab(const std::string& id,
                       Section section,
                       const std::string& parent,
                       int position) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto it = tabs_.find(id);
  if (it == tabs_.end()) {
    return false;
  }
  TabRecord& tab = it->second;
  if (!PlacementIsValid(tab.workspace_id, std::string(), parent)) {
    return false;
  }
  if (section == Section::kFavorite && tab.section != Section::kFavorite &&
      CountFavorites(tab.workspace_id, id) >= kFavoritesCap) {
    return false;
  }
  Placement from{tab.section, tab.parent, tab.position};
  // A tab that enters Favorites remembers where it came from; one that leaves
  // them forgets it (issue #180).
  if (section == Section::kFavorite && tab.section != Section::kFavorite) {
    tab.previous_section = tab.section;
  } else if (section != Section::kFavorite &&
             tab.section == Section::kFavorite) {
    tab.previous_section.reset();
  }
  tab.section = section;
  tab.parent = parent;
  tab.position = position;
  Stamp(tab, ChangeType::kMoved, RecordKind::kTab);
  observers_.Notify(&TabModelObserver::OnTabMoved, tab, from);
  NotifyChanged();
  return true;
}

bool TabModel::MoveFolder(const std::string& id,
                          Section section,
                          const std::string& parent,
                          int position) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto it = folders_.find(id);
  if (it == folders_.end()) {
    return false;
  }
  FolderRecord& folder = it->second;
  if (!PlacementIsValid(folder.workspace_id, id, parent)) {
    return false;
  }
  Placement from{folder.section, folder.parent, folder.position};
  folder.section = section;
  folder.parent = parent;
  folder.position = position;
  Stamp(folder, ChangeType::kMoved, RecordKind::kFolder);
  observers_.Notify(&TabModelObserver::OnFolderMoved, folder, from);
  NotifyChanged();
  return true;
}

bool TabModel::RemoveTab(const std::string& id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto it = tabs_.find(id);
  if (it == tabs_.end()) {
    return false;
  }
  TabRecord tab = std::move(it->second);
  tabs_.erase(it);
  AppendChange(ChangeType::kRemoved, RecordKind::kTab, id, clock_->Now());
  observers_.Notify(&TabModelObserver::OnTabRemoved, tab);
  NotifyChanged();
  return true;
}

bool TabModel::RemoveFolder(const std::string& id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto it = folders_.find(id);
  if (it == folders_.end()) {
    return false;
  }
  FolderRecord folder = std::move(it->second);
  folders_.erase(it);
  // What was inside moves up one level.
  for (auto& [tab_id, tab] : tabs_) {
    if (tab.parent == id) {
      Placement from{tab.section, tab.parent, tab.position};
      tab.parent = folder.parent;
      Stamp(tab, ChangeType::kMoved, RecordKind::kTab);
      observers_.Notify(&TabModelObserver::OnTabMoved, tab, from);
    }
  }
  for (auto& [folder_id, child] : folders_) {
    if (child.parent == id) {
      Placement from{child.section, child.parent, child.position};
      child.parent = folder.parent;
      Stamp(child, ChangeType::kMoved, RecordKind::kFolder);
      observers_.Notify(&TabModelObserver::OnFolderMoved, child, from);
    }
  }
  AppendChange(ChangeType::kRemoved, RecordKind::kFolder, id, clock_->Now());
  observers_.Notify(&TabModelObserver::OnFolderRemoved, folder);
  NotifyChanged();
  return true;
}

base::DictValue TabModel::ToValue() const {
  base::DictValue dict;
  dict.Set("version", kSchemaVersion);
  dict.Set("next_change", base::Int64ToValue(next_change_));
  dict.Set("workspace", WorkspaceToValue(workspace_));
  base::ListValue tabs;
  for (const auto& [id, tab] : tabs_) {
    tabs.Append(TabToValue(tab));
  }
  dict.Set("tabs", std::move(tabs));
  base::ListValue folders;
  for (const auto& [id, folder] : folders_) {
    folders.Append(FolderToValue(folder));
  }
  dict.Set("folders", std::move(folders));
  base::ListValue changes;
  for (const ChangeRecord& change : change_log_) {
    changes.Append(ChangeToValue(change));
  }
  dict.Set("changes", std::move(changes));
  return dict;
}

void TabModel::MergeLoaded(std::optional<ModelState> stored) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!stored) {
    if (workspace_.change == 0) {
      Stamp(workspace_, ChangeType::kAdded, RecordKind::kWorkspace);
      NotifyChanged();
    }
    observers_.Notify(&TabModelObserver::OnModelLoaded);
    return;
  }

  // Put this run's counters behind the stored ones.
  const int64_t shift = stored->next_change - 1;
  for (auto& [id, tab] : tabs_) {
    tab.change += shift;
  }
  for (auto& [id, folder] : folders_) {
    folder.change += shift;
  }
  for (ChangeRecord& change : change_log_) {
    change.counter += shift;
  }
  next_change_ += shift;

  std::deque<ChangeRecord> log;
  for (ChangeRecord& change : stored->changes) {
    log.push_back(std::move(change));
  }
  for (ChangeRecord& change : change_log_) {
    log.push_back(std::move(change));
  }
  change_log_ = std::move(log);
  while (change_log_.size() > max_change_log_entries_) {
    change_log_.pop_front();
  }

  // A Workspace edited before the read finished loses to the stored one: M2
  // has no editor, so nothing writes it that early.
  const std::string old_workspace_id = workspace_.id;
  workspace_ = std::move(stored->workspace);

  std::vector<std::string> stale;
  for (TabRecord& tab : stored->tabs) {
    if (tab.section == Section::kToday && !tab.sleeping && !tab.archived_at) {
      stale.push_back(tab.id);
    }
    tabs_.emplace(tab.id, std::move(tab));
  }
  for (FolderRecord& folder : stored->folders) {
    folders_.emplace(folder.id, std::move(folder));
  }

  if (workspace_.id != old_workspace_id) {
    for (auto& [id, tab] : tabs_) {
      if (tab.workspace_id == old_workspace_id) {
        tab.workspace_id = workspace_.id;
        Stamp(tab, ChangeType::kChanged, RecordKind::kTab);
      }
    }
    for (auto& [id, folder] : folders_) {
      if (folder.workspace_id == old_workspace_id) {
        folder.workspace_id = workspace_.id;
        Stamp(folder, ChangeType::kChanged, RecordKind::kFolder);
      }
    }
  }

  for (const std::string& id : stale) {
    RemoveTab(id);
  }
  NotifyChanged();
  observers_.Notify(&TabModelObserver::OnModelLoaded);
}

}  // namespace aerie_tab_model
