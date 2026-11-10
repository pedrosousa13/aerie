// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_ADBLOCK_BROWSER_CUSTOM_RULES_H_
#define COMPONENTS_AERIE_ADBLOCK_BROWSER_CUSTOM_RULES_H_

#include <string>
#include <vector>

namespace aerie_adblock {

// The user's own filter rules (issue #107) as plain data. The text is bounded
// and parsed in the browser process by the Rust core (AnalyzeCustomRules() in
// adblock_engine.h) before it is saved or applied.

// The id of the one list that holds the user's own rules. It is a file in the
// lists directory like any list, but has no address and is never downloaded.
inline constexpr char kCustomRulesId[] = "custom";

// Why a text of rules was not saved. Nothing changes when it is not kNone.
enum class CustomRulesFailure {
  kNone,
  kTooLarge,      // Over 256 KiB.
  kTooManyLines,  // Over 10 000 lines.
  kNotText,       // Not UTF-8.
  kTimeout,       // The parse used up its time budget.
  kUnwritable,    // The file could not be written.
  kSuperseded,    // A newer save arrived while this one was being checked.
};

// A fixed code for the settings page, which owns the words (a grd string per
// code). "" for kNone.
inline const char* CustomRulesFailureCode(CustomRulesFailure failure) {
  switch (failure) {
    case CustomRulesFailure::kNone:
      return "";
    case CustomRulesFailure::kTooLarge:
      return "too_large";
    case CustomRulesFailure::kTooManyLines:
      return "too_many_lines";
    case CustomRulesFailure::kNotText:
      return "not_text";
    case CustomRulesFailure::kTimeout:
      return "timeout";
    case CustomRulesFailure::kUnwritable:
      return "unwritable";
    case CustomRulesFailure::kSuperseded:
      return "";  // Not an error the user caused: the newer save answers.
  }
  return "";
}

// One line that is not in the engine.
struct CustomRuleIssue {
  int line = 0;  // 1-based.
  // A fixed code: "scriptlet", "html_filter", "procedural", "style_injection",
  // "unsafe_selector", "too_long", "contains_nul", "matches_every_address" or
  // "unparseable". The page owns the words.
  std::string reason;
  // True when the rule is left out because it is too broad, not because it is
  // a kind Aerie does not run.
  bool ignored = false;
  // The line, cut short. Untrusted text: show it as text only.
  std::string rule;
};

// What the saved text contained.
struct CustomRules {
  // The text as the user saved it (what the textarea shows).
  std::string text;
  int network_rules = 0;
  int hide_rules = 0;
  int ignored_exceptions = 0;  // Exception rules that match every address.
  int dropped = 0;             // Lines dropped for any other reason.
  int issue_count = 0;         // All issues; |issues| holds the first ones.
  std::vector<CustomRuleIssue> issues;
};

}  // namespace aerie_adblock

#endif  // COMPONENTS_AERIE_ADBLOCK_BROWSER_CUSTOM_RULES_H_
