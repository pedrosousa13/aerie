// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_TAB_MODEL_TAB_MODEL_TYPES_H_
#define COMPONENTS_AERIE_TAB_MODEL_TAB_MODEL_TYPES_H_

#include <stddef.h>
#include <stdint.h>

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "base/time/time.h"
#include "base/values.h"

// The records of the Tab Model (issue #176, spec #1 of issue #142). They are
// plain data: no Chromium tab, no profile, no file. The model that owns them
// is in tab_model.h.
//
// Every record has a stable id, `created`, `modified` and `change`. `change` is
// the model's change counter at the record's last write; it only grows. Together
// with the change log (ChangeRecord) it is what a later sync reads.
namespace aerie_tab_model {

// The version of state.json. Bump it, and add a step to the migration table in
// tab_model_types.cc, when a field changes meaning.
inline constexpr int kSchemaVersion = 1;

// How many change records the log keeps. The oldest go first.
inline constexpr size_t kMaxChangeLogEntries = 1000;

// The most Favorites one Workspace holds (the design says 12). A constant, not
// a pref.
inline constexpr size_t kFavoritesCap = 12;

// The id of the Workspace that a fresh profile gets. M2 has exactly one, so a
// fixed id lets tabs opened before state.json is read name their Workspace.
inline constexpr char kDefaultWorkspaceId[] = "personal";

enum class Section { kToday, kPinned, kFavorite };
enum class FolderKind { kManual, kAuto, kSmart };
enum class ChangeType { kAdded, kChanged, kMoved, kRemoved };
enum class RecordKind { kWorkspace, kTab, kFolder };

// The names are persisted values; do not rename them.
const char* SectionToString(Section section);
std::optional<Section> SectionFromString(std::string_view text);

struct Record {
  std::string id;
  base::Time created;
  base::Time modified;
  int64_t change = 0;
};

// The theme ids, mode values and colour preset names are persisted values
// (ADR 0007). The model stores them as text and does not check them: the editor
// that writes them owns the list.
struct Workspace : Record {
  std::string name;
  std::string icon;
  // One of the 8 presets, or a custom colour as "#rrggbb".
  std::string colour;
  std::string theme;
  std::string mode;
};

struct TabRecord : Record {
  std::string workspace_id;
  Section section = Section::kToday;
  // The id of the folder that holds this tab. Empty: the section itself.
  std::string parent;
  int position = 0;
  std::optional<std::string> pinned_url;
  std::optional<std::string> manual_name;
  base::Time last_active;
  bool sleeping = false;
  std::optional<base::Time> archived_at;
  // Where the tab was before it became a Favorite (issue #180): removing the
  // Favorite puts it back there. Set by TabModel::MoveTab when a tab enters
  // Favorites, cleared when it leaves them. Absent for every other tab.
  std::optional<Section> previous_section;
  // The page title as the tab last showed it. Kept for Pinned and Favorites
  // tabs only (issue #180): after a restart their tabs are created unloaded and
  // have no title until they load, so the row shows this one.
  std::optional<std::string> last_title;
};

struct FolderRecord : Record {
  std::string workspace_id;
  Section section = Section::kToday;
  std::string parent;
  int position = 0;
  std::string name;
  std::string icon;
  FolderKind kind = FolderKind::kManual;
  // Owned by the slices that define them (auto-filing rules and the folder
  // behaviours). Stored as they are.
  std::optional<base::DictValue> rules;
  std::optional<base::DictValue> behaviour;
};

struct ChangeRecord {
  // The model's change counter at this write; strictly increasing.
  int64_t counter = 0;
  std::string id;
  ChangeType type = ChangeType::kAdded;
  RecordKind kind = RecordKind::kTab;
  std::string record_id;
  base::Time time;
};

// All the model holds, as it is read from or written to state.json.
struct ModelState {
  Workspace workspace;
  std::vector<TabRecord> tabs;
  std::vector<FolderRecord> folders;
  std::vector<ChangeRecord> changes;
  // The counter value the next write gets. At least 1.
  int64_t next_change = 1;
};

enum class LoadStatus {
  kOk,
  // No state.json yet: a fresh profile.
  kMissing,
  // Not JSON, wrong shape, or an id used twice.
  kCorrupt,
  // Written by a newer build.
  kNewerSchema,
};

// The step at index i turns the JSON of version i + 1 into version i + 2. The
// table is empty while there is one version.
using MigrationStep = void (*)(base::DictValue&);

// Migrates `state` in place from its own `version` field up to `target`, and
// sets the field. Returns kOk, kCorrupt (no valid version, or a version below 1)
// or kNewerSchema (a version above `target`).
LoadStatus MigrateToVersion(base::DictValue& state,
                            std::span<const MigrationStep> steps,
                            int target);

struct ParseResult {
  ParseResult();
  ParseResult(ParseResult&&);
  ParseResult& operator=(ParseResult&&);
  ~ParseResult();

  LoadStatus status = LoadStatus::kCorrupt;
  // Set when `status` is kOk.
  std::optional<ModelState> state;
};

// Migrates (a copy of) `json` to kSchemaVersion and reads it. Strict: a record
// with a missing or wrongly typed field makes the whole file kCorrupt.
ParseResult ParseState(const base::DictValue& json);
// The same with an explicit table, for tests.
ParseResult ParseState(const base::DictValue& json,
                       std::span<const MigrationStep> steps,
                       int target);

base::DictValue StateToValue(const ModelState& state);

base::DictValue WorkspaceToValue(const Workspace& workspace);
base::DictValue TabToValue(const TabRecord& tab);
base::DictValue FolderToValue(const FolderRecord& folder);
base::DictValue ChangeToValue(const ChangeRecord& change);

std::optional<Workspace> WorkspaceFromValue(const base::DictValue& dict);
std::optional<TabRecord> TabFromValue(const base::DictValue& dict);
std::optional<FolderRecord> FolderFromValue(const base::DictValue& dict);
std::optional<ChangeRecord> ChangeFromValue(const base::DictValue& dict);

}  // namespace aerie_tab_model

#endif  // COMPONENTS_AERIE_TAB_MODEL_TAB_MODEL_TYPES_H_
