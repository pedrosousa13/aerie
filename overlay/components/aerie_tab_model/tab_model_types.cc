// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_tab_model/tab_model_types.h"

#include <set>
#include <utility>

#include "base/check.h"
#include "base/notreached.h"
#include "base/json/values_util.h"

namespace aerie_tab_model {

namespace {

// Steps from version N + 1 to N + 2, oldest first. Version 1 is the first.
constexpr std::span<const MigrationStep> kMigrations;

const char* FolderKindToString(FolderKind kind) {
  switch (kind) {
    case FolderKind::kManual:
      return "manual";
    case FolderKind::kAuto:
      return "auto";
    case FolderKind::kSmart:
      return "smart";
  }
  NOTREACHED();
}

std::optional<FolderKind> FolderKindFromString(std::string_view text) {
  if (text == "manual") {
    return FolderKind::kManual;
  }
  if (text == "auto") {
    return FolderKind::kAuto;
  }
  if (text == "smart") {
    return FolderKind::kSmart;
  }
  return std::nullopt;
}

const char* ChangeTypeToString(ChangeType type) {
  switch (type) {
    case ChangeType::kAdded:
      return "added";
    case ChangeType::kChanged:
      return "changed";
    case ChangeType::kMoved:
      return "moved";
    case ChangeType::kRemoved:
      return "removed";
  }
  NOTREACHED();
}

std::optional<ChangeType> ChangeTypeFromString(std::string_view text) {
  if (text == "added") {
    return ChangeType::kAdded;
  }
  if (text == "changed") {
    return ChangeType::kChanged;
  }
  if (text == "moved") {
    return ChangeType::kMoved;
  }
  if (text == "removed") {
    return ChangeType::kRemoved;
  }
  return std::nullopt;
}

const char* RecordKindToString(RecordKind kind) {
  switch (kind) {
    case RecordKind::kWorkspace:
      return "workspace";
    case RecordKind::kTab:
      return "tab";
    case RecordKind::kFolder:
      return "folder";
  }
  NOTREACHED();
}

std::optional<RecordKind> RecordKindFromString(std::string_view text) {
  if (text == "workspace") {
    return RecordKind::kWorkspace;
  }
  if (text == "tab") {
    return RecordKind::kTab;
  }
  if (text == "folder") {
    return RecordKind::kFolder;
  }
  return std::nullopt;
}

void WriteRecord(const Record& record, base::DictValue& dict) {
  dict.Set("id", record.id);
  dict.Set("created", base::TimeToValue(record.created));
  dict.Set("modified", base::TimeToValue(record.modified));
  dict.Set("change", base::Int64ToValue(record.change));
}

bool ReadRecord(const base::DictValue& dict, Record& record) {
  const std::string* id = dict.FindString("id");
  std::optional<base::Time> created = base::ValueToTime(dict.Find("created"));
  std::optional<base::Time> modified = base::ValueToTime(dict.Find("modified"));
  std::optional<int64_t> change = base::ValueToInt64(dict.Find("change"));
  if (!id || id->empty() || !created || !modified || !change || *change < 0) {
    return false;
  }
  record.id = *id;
  record.created = *created;
  record.modified = *modified;
  record.change = *change;
  return true;
}

bool ReadString(const base::DictValue& dict, const char* key, std::string& out) {
  const std::string* value = dict.FindString(key);
  if (!value) {
    return false;
  }
  out = *value;
  return true;
}

// A key that may be absent, but if present must be a string.
bool ReadOptionalString(const base::DictValue& dict,
                        const char* key,
                        std::optional<std::string>& out) {
  const base::Value* value = dict.Find(key);
  if (!value) {
    out.reset();
    return true;
  }
  if (!value->is_string()) {
    return false;
  }
  out = value->GetString();
  return true;
}

bool ReadOptionalDict(const base::DictValue& dict,
                      const char* key,
                      std::optional<base::DictValue>& out) {
  const base::Value* value = dict.Find(key);
  if (!value) {
    out.reset();
    return true;
  }
  if (!value->is_dict()) {
    return false;
  }
  out = value->GetDict().Clone();
  return true;
}

bool ReadPlacement(const base::DictValue& dict,
                   std::string& workspace_id,
                   Section& section,
                   std::string& parent,
                   int& position) {
  std::string section_text;
  std::optional<int> pos = dict.FindInt("position");
  if (!ReadString(dict, "workspace_id", workspace_id) ||
      !ReadString(dict, "section", section_text) ||
      !ReadString(dict, "parent", parent) || !pos) {
    return false;
  }
  std::optional<Section> parsed = SectionFromString(section_text);
  if (!parsed) {
    return false;
  }
  section = *parsed;
  position = *pos;
  return true;
}

base::DictValue ArchiveToValue(const ArchiveData& archive) {
  base::DictValue dict;
  dict.Set("url", archive.url);
  dict.Set("title", archive.title);
  dict.Set("favicon_key", archive.favicon_key);
  dict.Set("folder", archive.folder);
  base::ListValue entries;
  for (const ArchivedNavigation& entry : archive.entries) {
    base::DictValue item;
    item.Set("url", entry.url);
    item.Set("title", entry.title);
    entries.Append(std::move(item));
  }
  dict.Set("entries", std::move(entries));
  return dict;
}

std::optional<ArchiveData> ArchiveFromValue(const base::DictValue& dict) {
  ArchiveData archive;
  const base::ListValue* entries = dict.FindList("entries");
  if (!ReadString(dict, "url", archive.url) ||
      !ReadString(dict, "title", archive.title) ||
      !ReadString(dict, "favicon_key", archive.favicon_key) ||
      !ReadString(dict, "folder", archive.folder) || !entries ||
      entries->size() > kMaxArchivedNavigationEntries) {
    return std::nullopt;
  }
  for (const base::Value& value : *entries) {
    const base::DictValue* item = value.GetIfDict();
    ArchivedNavigation entry;
    if (!item || !ReadString(*item, "url", entry.url) ||
        !ReadString(*item, "title", entry.title)) {
      return std::nullopt;
    }
    archive.entries.push_back(std::move(entry));
  }
  return archive;
}

}  // namespace

const char* SectionToString(Section section) {
  switch (section) {
    case Section::kToday:
      return "today";
    case Section::kPinned:
      return "pinned";
    case Section::kFavorite:
      return "favorite";
  }
  NOTREACHED();
}

std::optional<Section> SectionFromString(std::string_view text) {
  if (text == "today") {
    return Section::kToday;
  }
  if (text == "pinned") {
    return Section::kPinned;
  }
  if (text == "favorite") {
    return Section::kFavorite;
  }
  return std::nullopt;
}

base::DictValue WorkspaceToValue(const Workspace& workspace) {
  base::DictValue dict;
  WriteRecord(workspace, dict);
  dict.Set("name", workspace.name);
  dict.Set("icon", workspace.icon);
  dict.Set("colour", workspace.colour);
  dict.Set("theme", workspace.theme);
  dict.Set("mode", workspace.mode);
  return dict;
}

std::optional<Workspace> WorkspaceFromValue(const base::DictValue& dict) {
  Workspace workspace;
  if (!ReadRecord(dict, workspace) || !ReadString(dict, "name", workspace.name) ||
      !ReadString(dict, "icon", workspace.icon) ||
      !ReadString(dict, "colour", workspace.colour) ||
      !ReadString(dict, "theme", workspace.theme) ||
      !ReadString(dict, "mode", workspace.mode)) {
    return std::nullopt;
  }
  return workspace;
}

base::DictValue TabToValue(const TabRecord& tab) {
  base::DictValue dict;
  WriteRecord(tab, dict);
  dict.Set("workspace_id", tab.workspace_id);
  dict.Set("section", SectionToString(tab.section));
  dict.Set("parent", tab.parent);
  dict.Set("position", tab.position);
  if (tab.pinned_url) {
    dict.Set("pinned_url", *tab.pinned_url);
  }
  if (tab.manual_name) {
    dict.Set("manual_name", *tab.manual_name);
  }
  dict.Set("last_active", base::TimeToValue(tab.last_active));
  dict.Set("sleeping", tab.sleeping);
  if (tab.archived_at) {
    dict.Set("archived_at", base::TimeToValue(*tab.archived_at));
  }
  if (tab.archive) {
    dict.Set("archive", ArchiveToValue(*tab.archive));
  }
  if (tab.previous_section) {
    dict.Set("previous_section", SectionToString(*tab.previous_section));
  }
  if (tab.last_title) {
    dict.Set("last_title", *tab.last_title);
  }
  if (tab.filed_by) {
    base::DictValue filed;
    filed.Set("folder_id", tab.filed_by->folder_id);
    filed.Set("rule_id", tab.filed_by->rule_id);
    dict.Set("filed_by", std::move(filed));
  }
  return dict;
}

std::optional<TabRecord> TabFromValue(const base::DictValue& dict) {
  TabRecord tab;
  std::optional<base::Time> last_active =
      base::ValueToTime(dict.Find("last_active"));
  std::optional<bool> sleeping = dict.FindBool("sleeping");
  if (!ReadRecord(dict, tab) ||
      !ReadPlacement(dict, tab.workspace_id, tab.section, tab.parent,
                     tab.position) ||
      !ReadOptionalString(dict, "pinned_url", tab.pinned_url) ||
      !ReadOptionalString(dict, "manual_name", tab.manual_name) ||
      !ReadOptionalString(dict, "last_title", tab.last_title) ||
      !last_active || !sleeping) {
    return std::nullopt;
  }
  tab.last_active = *last_active;
  tab.sleeping = *sleeping;
  if (const base::Value* archived = dict.Find("archived_at")) {
    std::optional<base::Time> time = base::ValueToTime(archived);
    if (!time) {
      return std::nullopt;
    }
    tab.archived_at = *time;
  }
  // Added by issue #183 without a schema bump: absent in older files.
  if (const base::Value* archive = dict.Find("archive")) {
    const base::DictValue* archive_dict = archive->GetIfDict();
    std::optional<ArchiveData> parsed =
        archive_dict ? ArchiveFromValue(*archive_dict) : std::nullopt;
    if (!parsed) {
      return std::nullopt;
    }
    tab.archive = std::move(*parsed);
  }
  // Added by issue #180 without a schema bump: absent in older files.
  if (const base::Value* previous = dict.Find("previous_section")) {
    std::optional<Section> section =
        previous->is_string() ? SectionFromString(previous->GetString())
                              : std::nullopt;
    if (!section) {
      return std::nullopt;
    }
    tab.previous_section = *section;
  }
  // Added by issue #139 without a schema bump: absent in older files.
  if (const base::Value* filed = dict.Find("filed_by")) {
    const base::DictValue* filed_dict = filed->GetIfDict();
    const std::string* folder_id =
        filed_dict ? filed_dict->FindString("folder_id") : nullptr;
    const std::string* rule_id =
        filed_dict ? filed_dict->FindString("rule_id") : nullptr;
    if (!folder_id || !rule_id) {
      return std::nullopt;
    }
    tab.filed_by = FiledBy{*folder_id, *rule_id};
  }
  return tab;
}

base::DictValue FolderToValue(const FolderRecord& folder) {
  base::DictValue dict;
  WriteRecord(folder, dict);
  dict.Set("workspace_id", folder.workspace_id);
  dict.Set("section", SectionToString(folder.section));
  dict.Set("parent", folder.parent);
  dict.Set("position", folder.position);
  dict.Set("name", folder.name);
  dict.Set("icon", folder.icon);
  dict.Set("kind", FolderKindToString(folder.kind));
  dict.Set("expanded", folder.expanded);
  if (folder.rules) {
    dict.Set("rules", folder.rules->Clone());
  }
  if (folder.behaviour) {
    dict.Set("behaviour", folder.behaviour->Clone());
  }
  return dict;
}

std::optional<FolderRecord> FolderFromValue(const base::DictValue& dict) {
  FolderRecord folder;
  std::string kind_text;
  if (!ReadRecord(dict, folder) ||
      !ReadPlacement(dict, folder.workspace_id, folder.section, folder.parent,
                     folder.position) ||
      !ReadString(dict, "name", folder.name) ||
      !ReadString(dict, "icon", folder.icon) ||
      !ReadString(dict, "kind", kind_text) ||
      !ReadOptionalDict(dict, "rules", folder.rules) ||
      !ReadOptionalDict(dict, "behaviour", folder.behaviour)) {
    return std::nullopt;
  }
  std::optional<FolderKind> kind = FolderKindFromString(kind_text);
  if (!kind) {
    return std::nullopt;
  }
  folder.kind = *kind;
  // Added by issue #181 without a schema bump: absent in older files (no folder
  // was drawn before), which read as collapsed.
  if (const base::Value* expanded = dict.Find("expanded")) {
    if (!expanded->is_bool()) {
      return std::nullopt;
    }
    folder.expanded = expanded->GetBool();
  }
  return folder;
}

base::DictValue ChangeToValue(const ChangeRecord& change) {
  base::DictValue dict;
  dict.Set("counter", base::Int64ToValue(change.counter));
  dict.Set("id", change.id);
  dict.Set("type", ChangeTypeToString(change.type));
  dict.Set("kind", RecordKindToString(change.kind));
  dict.Set("record_id", change.record_id);
  dict.Set("time", base::TimeToValue(change.time));
  return dict;
}

std::optional<ChangeRecord> ChangeFromValue(const base::DictValue& dict) {
  ChangeRecord change;
  std::optional<int64_t> counter = base::ValueToInt64(dict.Find("counter"));
  std::optional<base::Time> time = base::ValueToTime(dict.Find("time"));
  std::string type_text;
  std::string kind_text;
  if (!counter || *counter < 1 || !time || !ReadString(dict, "id", change.id) ||
      !ReadString(dict, "type", type_text) ||
      !ReadString(dict, "kind", kind_text) ||
      !ReadString(dict, "record_id", change.record_id)) {
    return std::nullopt;
  }
  std::optional<ChangeType> type = ChangeTypeFromString(type_text);
  std::optional<RecordKind> kind = RecordKindFromString(kind_text);
  if (!type || !kind) {
    return std::nullopt;
  }
  change.counter = *counter;
  change.type = *type;
  change.kind = *kind;
  change.time = *time;
  return change;
}

base::DictValue StateToValue(const ModelState& state) {
  base::DictValue dict;
  dict.Set("version", kSchemaVersion);
  dict.Set("next_change", base::Int64ToValue(state.next_change));
  dict.Set("workspace", WorkspaceToValue(state.workspace));
  base::ListValue tabs;
  for (const TabRecord& tab : state.tabs) {
    tabs.Append(TabToValue(tab));
  }
  dict.Set("tabs", std::move(tabs));
  base::ListValue folders;
  for (const FolderRecord& folder : state.folders) {
    folders.Append(FolderToValue(folder));
  }
  dict.Set("folders", std::move(folders));
  base::ListValue changes;
  for (const ChangeRecord& change : state.changes) {
    changes.Append(ChangeToValue(change));
  }
  dict.Set("changes", std::move(changes));
  return dict;
}

LoadStatus MigrateToVersion(base::DictValue& state,
                            std::span<const MigrationStep> steps,
                            int target) {
  CHECK_GE(target, 1);
  CHECK_GE(steps.size(), static_cast<size_t>(target - 1));
  std::optional<int> version = state.FindInt("version");
  if (!version || *version < 1) {
    return LoadStatus::kCorrupt;
  }
  if (*version > target) {
    return LoadStatus::kNewerSchema;
  }
  for (int v = *version; v < target; ++v) {
    steps[v - 1](state);
  }
  state.Set("version", target);
  return LoadStatus::kOk;
}

ParseResult::ParseResult() = default;
ParseResult::ParseResult(ParseResult&&) = default;
ParseResult& ParseResult::operator=(ParseResult&&) = default;
ParseResult::~ParseResult() = default;

ParseResult ParseState(const base::DictValue& json) {
  return ParseState(json, kMigrations, kSchemaVersion);
}

ParseResult ParseState(const base::DictValue& json,
                       std::span<const MigrationStep> steps,
                       int target) {
  ParseResult result;
  base::DictValue migrated = json.Clone();
  result.status = MigrateToVersion(migrated, steps, target);
  if (result.status != LoadStatus::kOk) {
    return result;
  }
  result.status = LoadStatus::kCorrupt;

  ModelState state;
  std::optional<int64_t> next = base::ValueToInt64(migrated.Find("next_change"));
  const base::DictValue* workspace = migrated.FindDict("workspace");
  const base::ListValue* tabs = migrated.FindList("tabs");
  const base::ListValue* folders = migrated.FindList("folders");
  const base::ListValue* changes = migrated.FindList("changes");
  if (!next || *next < 1 || !workspace || !tabs || !folders || !changes) {
    return result;
  }
  state.next_change = *next;

  std::set<std::string> ids;
  std::optional<Workspace> parsed_workspace = WorkspaceFromValue(*workspace);
  if (!parsed_workspace || parsed_workspace->change >= state.next_change) {
    return result;
  }
  ids.insert(parsed_workspace->id);
  state.workspace = std::move(*parsed_workspace);

  for (const base::Value& item : *tabs) {
    const base::DictValue* dict = item.GetIfDict();
    std::optional<TabRecord> tab =
        dict ? TabFromValue(*dict) : std::nullopt;
    if (!tab || tab->change >= state.next_change ||
        !ids.insert(tab->id).second) {
      return result;
    }
    state.tabs.push_back(std::move(*tab));
  }
  for (const base::Value& item : *folders) {
    const base::DictValue* dict = item.GetIfDict();
    std::optional<FolderRecord> folder =
        dict ? FolderFromValue(*dict) : std::nullopt;
    if (!folder || folder->change >= state.next_change ||
        !ids.insert(folder->id).second) {
      return result;
    }
    state.folders.push_back(std::move(*folder));
  }
  int64_t previous = 0;
  for (const base::Value& item : *changes) {
    const base::DictValue* dict = item.GetIfDict();
    std::optional<ChangeRecord> change =
        dict ? ChangeFromValue(*dict) : std::nullopt;
    if (!change || change->counter <= previous ||
        change->counter >= state.next_change) {
      return result;
    }
    previous = change->counter;
    state.changes.push_back(std::move(*change));
  }
  result.status = LoadStatus::kOk;
  result.state = std::move(state);
  return result;
}

}  // namespace aerie_tab_model
