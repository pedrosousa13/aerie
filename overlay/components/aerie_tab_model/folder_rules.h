// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_TAB_MODEL_FOLDER_RULES_H_
#define COMPONENTS_AERIE_TAB_MODEL_FOLDER_RULES_H_

#include <optional>
#include <string>
#include <string_view>

#include "components/aerie_tab_model/rule_matcher.h"
#include "components/aerie_tab_model/tab_model_types.h"

class GURL;

// The rules of an auto-filing folder, as the Tab Model stores them (issue #139,
// docs/sidebar-design.md, "Folders"), and the edits the folder editor sheet and
// the auto-filing controller make. Plain logic over the model, so it has unit
// tests. The matcher itself is rule_matcher.h.
//
// The rules live in `FolderRecord::rules` (a dictionary the schema leaves to
// the slice that defines it):
//   { "enabled": bool, "rules": [ {id, type, value, enabled} ... ],
//     "excluded_hosts": [string...], "excluded_tabs": [string...] }
// A folder files tabs only while its kind is Auto-filing. Switching it to Manual
// keeps the rules and the tab only stops filing (the rules do not apply).
namespace aerie_tab_model {

class TabModel;

// How many rules and exceptions one folder holds. A guard against a runaway
// UI, not a design limit.
inline constexpr size_t kMaxRulesPerFolder = 50;
inline constexpr size_t kMaxExceptionsPerFolder = 200;
// The longest value of a rule or exception, in bytes.
inline constexpr size_t kMaxRuleValueLength = 512;

// The rules of a folder record. A record with none, or with a damaged
// dictionary, reads as the default: on, no rules. A rule with an unknown type
// is dropped.
FolderRules ReadFolderRules(const FolderRecord& folder);
base::DictValue FolderRulesToValue(const FolderRules& rules);

// True when the folder takes part in auto-filing: its kind is Auto-filing.
bool IsAutoFiling(const FolderRecord& folder);

// --- Edits. Each writes the folder record at once (live apply). ---

// Auto-filing and Manual only; Smart is issue #140's, so it is refused here.
// Switching to Manual keeps the rules.
bool SetFolderRuleKind(TabModel& model, const std::string& folder_id, FolderKind kind);

// Adds a rule at the end and returns its id. Empty for an unknown folder, a
// blank or too long value, or a folder that has kMaxRulesPerFolder already.
std::string AddRule(TabModel& model,
                    const std::string& folder_id,
                    RuleType type,
                    std::string_view value);
// Changes the type and value of a rule. False for an unknown folder or rule or a
// blank value.
bool UpdateRule(TabModel& model,
                const std::string& folder_id,
                const std::string& rule_id,
                RuleType type,
                std::string_view value);
bool RemoveRule(TabModel& model, const std::string& folder_id, const std::string& rule_id);
bool SetRuleEnabled(TabModel& model,
                    const std::string& folder_id,
                    const std::string& rule_id,
                    bool enabled);
// The folder's "All" switch.
bool SetFolderRulesEnabled(TabModel& model, const std::string& folder_id, bool enabled);

// "Don't file this site": the host stays out of the folder, subdomains too.
// False for an unknown folder, a blank host, or when it is there already.
bool AddExcludedHost(TabModel& model, const std::string& folder_id, std::string_view host);
bool RemoveExcludedHost(TabModel& model, const std::string& folder_id, std::string_view host);
// "Stop this rule matching this tab". False for an unknown folder or tab, or
// when it is there already.
bool AddExcludedTab(TabModel& model, const std::string& folder_id, const std::string& tab_id);

// --- Finding the folder for a new tab ---

struct AutoFileTarget {
  std::string folder_id;
  std::string rule_id;
};

// The folder a new tab with this address and title goes into, or nothing. The
// folders are asked in order: the Pinned folders first, depth first from the top,
// then the Today folders. The first folder with a match wins. Only folders of
// the open Workspace whose kind is Auto-filing are asked; `rules_enabled` is
// the global switch ("Folder rules everywhere"), and when it is off nothing
// matches. A folder the tab cannot go into (it would be nested too deep) is
// passed over.
std::optional<AutoFileTarget> FindAutoFileTarget(const TabModel& model,
                                                 const GURL& url,
                                                 std::string_view title,
                                                 const std::string& tab_id,
                                                 bool rules_enabled);

// Files the tab into the folder: the tab goes to the top of the folder and
// records `filed_by`. A tab filed into a Pinned folder becomes a Pinned tab (its
// pinned URL is `pinned_url`). Returns false, and changes nothing, when the tab
// or the folder is unknown, or the tab cannot go in.
bool FileTabIntoFolder(TabModel& model,
                       const std::string& tab_id,
                       const AutoFileTarget& target,
                       const std::string& pinned_url);

// Undo: the tab goes back to the top of Today (a Pinned tab loses its pinned
// URL) and `filed_by` is cleared. False when the tab is not filed (it left the
// folder some other way), or is unknown.
bool UndoFiling(TabModel& model, const std::string& tab_id);

// "Stop this rule matching this tab": adds the tab to the exceptions of the
// folder that filed it. False when the tab is not filed.
bool StopRuleMatchingTab(TabModel& model, const std::string& tab_id);

}  // namespace aerie_tab_model

#endif  // COMPONENTS_AERIE_TAB_MODEL_FOLDER_RULES_H_
