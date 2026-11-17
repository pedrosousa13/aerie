// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_TAB_MODEL_FOLDER_BEHAVIOUR_H_
#define COMPONENTS_AERIE_TAB_MODEL_FOLDER_BEHAVIOUR_H_

#include <optional>
#include <string>
#include <string_view>

#include "base/time/time.h"
#include "components/aerie_tab_model/tab_model_types.h"

// Folder behaviour (issue #141, docs/sidebar-design.md, "Folder behaviour"):
// three settings that a folder can set for the tabs in it, and the one rule
// that decides which value a tab gets. Plain logic over the Tab Model, so it has
// unit tests.
//
// A folder stores what it sets in `FolderRecord::behaviour`, a dictionary the
// schema leaves to this slice:
//   { "archive_after": "never" | "12h" | "24h" | "7d",
//     "sleep_after_minutes": 0 (never) | 15 ... 480,
//     "links": "new-tab" | "in-place" }
// A key that is absent means "Inherits global", and so does a value that is not
// one of those (a damaged file). Smart folders (issue #140) have no behaviour:
// they hold no tabs, so each tab keeps the behaviour of the folder it lives in.
//
// The rule (Effective): from the tab's own folder up through its parents, the
// nearest folder that sets the behaviour wins; if none does, the global value.
// Auto-archive has one more rule: a tab in Pinned, or in a Pinned folder, never
// archives, whatever any folder or the global setting says.
namespace aerie_tab_model {

class TabModel;

// The values of a folder's own auto-archive setting. The global setting has one
// more value than these three timeouts only in how it is stored (a pref).
enum class ArchiveChoice { kNever, k12Hours, k24Hours, k7Days };

enum class LinksOpen { kNewTab, kInPlace };

// What a folder sets itself. An empty member is "Inherits global".
struct FolderBehaviour {
  std::optional<ArchiveChoice> archive_after;
  // 0 is never; otherwise kMinSleepAfterMinutes to kMaxSleepAfterMinutes.
  std::optional<int> sleep_after_minutes;
  std::optional<LinksOpen> links;

  bool operator==(const FolderBehaviour&) const = default;
};

// The global values, as the prefs hold them. nullopt is "never".
struct GlobalBehaviour {
  std::optional<base::TimeDelta> archive_after;
  std::optional<base::TimeDelta> sleep_after;
  LinksOpen links = LinksOpen::kNewTab;
};

// One resolved value and where it came from: the id of the folder that sets it,
// or empty when it is the global value (or, for archive, the Pinned rule).
template <typename T>
struct Resolved {
  T value;
  std::string source_folder;
};

// The persisted names.
const char* ArchiveChoiceToString(ArchiveChoice choice);
std::optional<ArchiveChoice> ArchiveChoiceFromString(std::string_view text);
const char* LinksOpenToString(LinksOpen links);
std::optional<LinksOpen> LinksOpenFromString(std::string_view text);
// The delay of a choice; nullopt is never.
std::optional<base::TimeDelta> ArchiveChoiceDelay(ArchiveChoice choice);

// The sleep minutes a stored number means: 0, or clamped to the range of the
// global setting. A negative number reads as "not set".
std::optional<int> NormalizeSleepMinutes(int minutes);

// What the folder sets. A record with no behaviour, or a damaged dictionary,
// sets nothing. A smart folder sets nothing.
FolderBehaviour ReadFolderBehaviour(const FolderRecord& folder);

// --- Edits. Each writes the folder record at once (live apply); nullopt
// clears the setting ("Inherits global"). They return false for an unknown
// folder, a smart folder, a value that changes nothing, and (archive) a folder
// in Pinned, where the setting is disabled. ---
bool SetFolderArchiveAfter(TabModel& model,
                           const std::string& folder_id,
                           std::optional<ArchiveChoice> choice);
// `minutes` is passed through NormalizeSleepMinutes; a negative number clears.
bool SetFolderSleepAfter(TabModel& model,
                         const std::string& folder_id,
                         std::optional<int> minutes);
bool SetFolderLinksOpen(TabModel& model,
                        const std::string& folder_id,
                        std::optional<LinksOpen> links);

// --- Effective(tab, behaviour) ---

// The value a tab gets. nullopt is never.
Resolved<std::optional<base::TimeDelta>> ResolveArchiveAfter(
    const TabModel& model,
    const TabRecord& tab,
    const GlobalBehaviour& global);
Resolved<std::optional<base::TimeDelta>> ResolveSleepAfter(
    const TabModel& model,
    const TabRecord& tab,
    const GlobalBehaviour& global);
Resolved<LinksOpen> ResolveLinksOpen(const TabModel& model,
                                     const TabRecord& tab,
                                     const GlobalBehaviour& global);

// What a folder would get if it did not set the behaviour itself: the value
// from its parents, or the global one. The folder editor sheet shows it on the
// "Inherits" row. `source_folder` names the parent that sets it.
Resolved<std::optional<base::TimeDelta>> InheritedArchiveAfter(
    const TabModel& model,
    const FolderRecord& folder,
    const GlobalBehaviour& global);
Resolved<std::optional<base::TimeDelta>> InheritedSleepAfter(
    const TabModel& model,
    const FolderRecord& folder,
    const GlobalBehaviour& global);
Resolved<LinksOpen> InheritedLinksOpen(const TabModel& model,
                                       const FolderRecord& folder,
                                       const GlobalBehaviour& global);

// Shorthands for the schedules, which want only the value.
std::optional<base::TimeDelta> EffectiveArchiveAfter(
    const TabModel& model,
    const TabRecord& tab,
    const GlobalBehaviour& global);
std::optional<base::TimeDelta> EffectiveSleepAfter(
    const TabModel& model,
    const TabRecord& tab,
    const GlobalBehaviour& global);
LinksOpen EffectiveLinksOpen(const TabModel& model,
                             const TabRecord& tab,
                             const GlobalBehaviour& global);

}  // namespace aerie_tab_model

#endif  // COMPONENTS_AERIE_TAB_MODEL_FOLDER_BEHAVIOUR_H_
