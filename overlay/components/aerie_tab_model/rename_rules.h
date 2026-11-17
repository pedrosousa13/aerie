// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_TAB_MODEL_RENAME_RULES_H_
#define COMPONENTS_AERIE_TAB_MODEL_RENAME_RULES_H_

#include <stddef.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/values.h"
#include "components/aerie_tab_model/rule_matcher.h"

// The rename rules (issue #146, docs/sidebar-design.md, "Rename rules"): a user
// rule matches a tab by the shared rule grammar (rule_matcher.h) and rewrites the
// title the sidebar shows. No regular expression and no `$1` capture: a rewrite
// is a template with two placeholders, or "remove text". The rules live in a
// profile pref, so everything read from it is untrusted and checked here.
namespace aerie_tab_model {

// The most rules a profile holds, and the longest name, match value and
// rewrite text. A rule beyond the count, or longer than a limit, is dropped
// (the count) or the rule is refused (the lengths) when the pref is read.
inline constexpr size_t kMaxRenameRules = 50;
inline constexpr size_t kMaxRenameRuleNameBytes = 100;
inline constexpr size_t kMaxRenameRuleTextBytes = 512;

enum class RewriteMode {
  // `text` is a template: "{title}" is the page title (without the matched
  // text, for a Title contains rule) and "{host}" is the host without "www.".
  kTemplate,
  // Delete the matched text from the title. `text` is the text to delete; when
  // it is empty and the rule is Title contains, the rule's own value is.
  kRemoveText,
};

// The names are persisted values; do not rename them.
const char* RewriteModeToString(RewriteMode mode);
bool RewriteModeFromString(std::string_view text, RewriteMode* mode);

struct RenameRule {
  // 1 to 64 characters of A-Z a-z 0-9 - _ . Unique in a list.
  std::string id;
  std::string name;
  // `match.enabled` is not used; the rule's own switch is `enabled`.
  Rule match;
  RewriteMode mode = RewriteMode::kTemplate;
  std::string text;
  bool enabled = true;

  bool operator==(const RenameRule&) const = default;
};

struct RenameRules {
  // The "All rules" switch.
  bool enabled = true;
  std::vector<RenameRule> rules;

  bool operator==(const RenameRules&) const = default;
};

// A rule from a pref value, or nothing when anything is wrong with it (a field
// of the wrong type, a bad id, a type or mode that is unknown, an empty match
// value, a Domain value with no host in it, a text over the limits). A Domain
// value is made canonical (NormalizeDomain). A missing `enabled` counts as on.
std::optional<RenameRule> RenameRuleFromValue(const base::DictValue& dict);
base::DictValue RenameRuleToValue(const RenameRule& rule);

// The rules of a pref list: each invalid entry is dropped, so is a repeated id
// and everything past kMaxRenameRules.
std::vector<RenameRule> RenameRulesFromList(const base::ListValue& list);
base::ListValue RenameRulesToList(const std::vector<RenameRule>& rules);

// True when `id` is a well-formed rule id.
bool IsValidRenameRuleId(std::string_view id);

// What a rule makes of a title.
struct RenameOutcome {
  std::string text;
  const RenameRule* rule = nullptr;
};

// The result of the first enabled rule that matches `input` and makes a
// non-empty title different from `title`; nothing when "All rules" is off or no
// rule does. Reading goes in list order, so reordering the list changes which
// rule wins.
std::optional<RenameOutcome> ApplyRenameRules(const RenameRules& rules,
                                              const MatchInput& input,
                                              std::string_view title);

// One rule on its own, for the editor's test box. Nothing when it does not match
// or makes nothing new.
std::optional<std::string> ApplyRenameRule(const RenameRule& rule,
                                           const MatchInput& input,
                                           std::string_view title);

}  // namespace aerie_tab_model

#endif  // COMPONENTS_AERIE_TAB_MODEL_RENAME_RULES_H_
