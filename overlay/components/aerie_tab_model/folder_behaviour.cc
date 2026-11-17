// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_tab_model/folder_behaviour.h"

#include <algorithm>
#include <utility>

#include "base/functional/function_ref.h"
#include "base/values.h"
#include "components/aerie_tab_model/folder_tree.h"
#include "components/aerie_tab_model/tab_model.h"

namespace aerie_tab_model {

namespace {

constexpr char kArchiveAfterKey[] = "archive_after";
constexpr char kSleepAfterKey[] = "sleep_after_minutes";
constexpr char kLinksKey[] = "links";

// The sleep limits are those of the global setting (tab_model_prefs.h); they are
// repeated here because this target does not depend on the prefs.
constexpr int kMinSleepMinutes = 15;
constexpr int kMaxSleepMinutes = 8 * 60;

// Runs `edit` on the behaviour of the folder and writes it back. `edit` returns
// false to leave the record alone.
bool EditBehaviour(TabModel& model,
                   const std::string& folder_id,
                   bool for_archive,
                   base::FunctionRef<bool(FolderBehaviour&)> edit) {
  const FolderRecord* folder = model.GetFolder(folder_id);
  if (!folder || folder->kind == FolderKind::kSmart ||
      (for_archive && folder->section == Section::kPinned)) {
    return false;
  }
  FolderBehaviour behaviour = ReadFolderBehaviour(*folder);
  if (!edit(behaviour)) {
    return false;
  }
  base::DictValue dict;
  if (behaviour.archive_after) {
    dict.Set(kArchiveAfterKey, ArchiveChoiceToString(*behaviour.archive_after));
  }
  if (behaviour.sleep_after_minutes) {
    dict.Set(kSleepAfterKey, *behaviour.sleep_after_minutes);
  }
  if (behaviour.links) {
    dict.Set(kLinksKey, LinksOpenToString(*behaviour.links));
  }
  const bool empty = dict.empty();
  return model.UpdateFolder(folder_id, [&](FolderRecord& record) {
    if (empty) {
      record.behaviour.reset();
    } else {
      record.behaviour = std::move(dict);
    }
  });
}

// Walks from `start_id` (a folder id, or empty for the section itself) up
// through the parents. Returns the first folder whose behaviour `pick` says
// sets the value, and whether the walk passed a folder of Pinned.
struct Found {
  const FolderRecord* folder = nullptr;
  FolderBehaviour behaviour;
  bool in_pinned = false;
};

Found Walk(const TabModel& model,
           const std::string& start_id,
           base::FunctionRef<bool(const FolderBehaviour&)> sets) {
  Found found;
  std::string id = start_id;
  for (int i = 0; i < kWalkBound && !id.empty(); ++i) {
    const FolderRecord* folder = model.GetFolder(id);
    if (!folder) {
      break;
    }
    if (folder->section == Section::kPinned) {
      found.in_pinned = true;
    }
    if (!found.folder) {
      FolderBehaviour behaviour = ReadFolderBehaviour(*folder);
      if (sets(behaviour)) {
        found.folder = folder;
        found.behaviour = behaviour;
      }
    }
    id = folder->parent;
  }
  return found;
}

Resolved<std::optional<base::TimeDelta>> ArchiveFrom(
    const TabModel& model,
    const std::string& start_id,
    Section section,
    const GlobalBehaviour& global) {
  Found found = Walk(model, start_id, [](const FolderBehaviour& behaviour) {
    return behaviour.archive_after.has_value();
  });
  // Pinned tabs never archive: the rule outranks every setting.
  if (section != Section::kToday || found.in_pinned) {
    return {std::nullopt, std::string()};
  }
  if (found.folder) {
    return {ArchiveChoiceDelay(*found.behaviour.archive_after),
            found.folder->id};
  }
  return {global.archive_after, std::string()};
}

Resolved<std::optional<base::TimeDelta>> SleepFrom(
    const TabModel& model,
    const std::string& start_id,
    const GlobalBehaviour& global) {
  Found found = Walk(model, start_id, [](const FolderBehaviour& behaviour) {
    return behaviour.sleep_after_minutes.has_value();
  });
  if (found.folder) {
    const int minutes = *found.behaviour.sleep_after_minutes;
    return {minutes == 0 ? std::nullopt
                         : std::optional<base::TimeDelta>(
                               base::Minutes(minutes)),
            found.folder->id};
  }
  return {global.sleep_after, std::string()};
}

Resolved<LinksOpen> LinksFrom(const TabModel& model,
                              const std::string& start_id,
                              const GlobalBehaviour& global) {
  Found found = Walk(model, start_id, [](const FolderBehaviour& behaviour) {
    return behaviour.links.has_value();
  });
  if (found.folder) {
    return {*found.behaviour.links, found.folder->id};
  }
  return {global.links, std::string()};
}

}  // namespace

const char* ArchiveChoiceToString(ArchiveChoice choice) {
  switch (choice) {
    case ArchiveChoice::kNever:
      return "never";
    case ArchiveChoice::k12Hours:
      return "12h";
    case ArchiveChoice::k24Hours:
      return "24h";
    case ArchiveChoice::k7Days:
      return "7d";
  }
  return "never";
}

std::optional<ArchiveChoice> ArchiveChoiceFromString(std::string_view text) {
  for (ArchiveChoice choice :
       {ArchiveChoice::kNever, ArchiveChoice::k12Hours, ArchiveChoice::k24Hours,
        ArchiveChoice::k7Days}) {
    if (text == ArchiveChoiceToString(choice)) {
      return choice;
    }
  }
  return std::nullopt;
}

const char* LinksOpenToString(LinksOpen links) {
  return links == LinksOpen::kInPlace ? "in-place" : "new-tab";
}

std::optional<LinksOpen> LinksOpenFromString(std::string_view text) {
  if (text == "new-tab") {
    return LinksOpen::kNewTab;
  }
  if (text == "in-place") {
    return LinksOpen::kInPlace;
  }
  return std::nullopt;
}

std::optional<base::TimeDelta> ArchiveChoiceDelay(ArchiveChoice choice) {
  switch (choice) {
    case ArchiveChoice::kNever:
      return std::nullopt;
    case ArchiveChoice::k12Hours:
      return base::Hours(12);
    case ArchiveChoice::k24Hours:
      return base::Hours(24);
    case ArchiveChoice::k7Days:
      return base::Days(7);
  }
  return std::nullopt;
}

std::optional<int> NormalizeSleepMinutes(int minutes) {
  if (minutes < 0) {
    return std::nullopt;
  }
  if (minutes == 0) {
    return 0;
  }
  return std::clamp(minutes, kMinSleepMinutes, kMaxSleepMinutes);
}

FolderBehaviour ReadFolderBehaviour(const FolderRecord& folder) {
  FolderBehaviour behaviour;
  if (!folder.behaviour || folder.kind == FolderKind::kSmart) {
    return behaviour;
  }
  const base::DictValue& dict = *folder.behaviour;
  if (const std::string* text = dict.FindString(kArchiveAfterKey)) {
    behaviour.archive_after = ArchiveChoiceFromString(*text);
  }
  if (std::optional<int> minutes = dict.FindInt(kSleepAfterKey)) {
    behaviour.sleep_after_minutes = NormalizeSleepMinutes(*minutes);
  }
  if (const std::string* text = dict.FindString(kLinksKey)) {
    behaviour.links = LinksOpenFromString(*text);
  }
  return behaviour;
}

bool SetFolderArchiveAfter(TabModel& model,
                           const std::string& folder_id,
                           std::optional<ArchiveChoice> choice) {
  return EditBehaviour(model, folder_id, /*for_archive=*/true,
                       [&](FolderBehaviour& behaviour) {
                         if (behaviour.archive_after == choice) {
                           return false;
                         }
                         behaviour.archive_after = choice;
                         return true;
                       });
}

bool SetFolderSleepAfter(TabModel& model,
                         const std::string& folder_id,
                         std::optional<int> minutes) {
  std::optional<int> stored =
      minutes ? NormalizeSleepMinutes(*minutes) : std::nullopt;
  return EditBehaviour(model, folder_id, /*for_archive=*/false,
                       [&](FolderBehaviour& behaviour) {
                         if (behaviour.sleep_after_minutes == stored) {
                           return false;
                         }
                         behaviour.sleep_after_minutes = stored;
                         return true;
                       });
}

bool SetFolderLinksOpen(TabModel& model,
                        const std::string& folder_id,
                        std::optional<LinksOpen> links) {
  return EditBehaviour(model, folder_id, /*for_archive=*/false,
                       [&](FolderBehaviour& behaviour) {
                         if (behaviour.links == links) {
                           return false;
                         }
                         behaviour.links = links;
                         return true;
                       });
}

Resolved<std::optional<base::TimeDelta>> ResolveArchiveAfter(
    const TabModel& model,
    const TabRecord& tab,
    const GlobalBehaviour& global) {
  return ArchiveFrom(model, tab.parent, tab.section, global);
}

Resolved<std::optional<base::TimeDelta>> ResolveSleepAfter(
    const TabModel& model,
    const TabRecord& tab,
    const GlobalBehaviour& global) {
  return SleepFrom(model, tab.parent, global);
}

Resolved<LinksOpen> ResolveLinksOpen(const TabModel& model,
                                     const TabRecord& tab,
                                     const GlobalBehaviour& global) {
  return LinksFrom(model, tab.parent, global);
}

Resolved<std::optional<base::TimeDelta>> InheritedArchiveAfter(
    const TabModel& model,
    const FolderRecord& folder,
    const GlobalBehaviour& global) {
  // The folder's own section decides the Pinned rule, as it does for a tab.
  Resolved<std::optional<base::TimeDelta>> resolved =
      ArchiveFrom(model, folder.parent, folder.section, global);
  return resolved;
}

Resolved<std::optional<base::TimeDelta>> InheritedSleepAfter(
    const TabModel& model,
    const FolderRecord& folder,
    const GlobalBehaviour& global) {
  return SleepFrom(model, folder.parent, global);
}

Resolved<LinksOpen> InheritedLinksOpen(const TabModel& model,
                                       const FolderRecord& folder,
                                       const GlobalBehaviour& global) {
  return LinksFrom(model, folder.parent, global);
}

std::optional<base::TimeDelta> EffectiveArchiveAfter(
    const TabModel& model,
    const TabRecord& tab,
    const GlobalBehaviour& global) {
  return ResolveArchiveAfter(model, tab, global).value;
}

std::optional<base::TimeDelta> EffectiveSleepAfter(
    const TabModel& model,
    const TabRecord& tab,
    const GlobalBehaviour& global) {
  return ResolveSleepAfter(model, tab, global).value;
}

LinksOpen EffectiveLinksOpen(const TabModel& model,
                             const TabRecord& tab,
                             const GlobalBehaviour& global) {
  return ResolveLinksOpen(model, tab, global).value;
}

}  // namespace aerie_tab_model
