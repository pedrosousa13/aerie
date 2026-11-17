// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_tab_model/rename_rules.h"

#include <set>
#include <utility>
#include <vector>

#include "base/i18n/case_conversion.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "components/aerie_tab_model/title_resolver.h"
#include "components/aerie_tab_model/title_tidier.h"

namespace aerie_tab_model {

namespace {

// How many times "Remove text" looks for the text again. A title is at most a
// few hundred characters, so this is more than enough and bounds the work.
constexpr int kMaxRemovals = 16;

bool OnlyRuleIdChars(std::string_view id) {
  for (char c : id) {
    if (!(base::IsAsciiAlpha(c) || base::IsAsciiDigit(c) || c == '-' ||
          c == '_')) {
      return false;
    }
  }
  return true;
}

// Finds `needle` in `haystack` (UTF-16 offsets), ignoring case only, as the
// rule matcher does for "Title contains": both sides are case-folded and an
// accent is a different letter. `index` and `length` are in `haystack`.
bool FindIgnoringCase(const std::u16string& needle,
                      const std::u16string& haystack,
                      size_t* index,
                      size_t* length) {
  const std::u16string folded_needle = base::i18n::FoldCase(needle);
  if (folded_needle.empty()) {
    return false;
  }
  // Fold one character at a time, so that a position in the folded text maps
  // back to a position in the title (folding can change the length).
  std::u16string folded;
  std::vector<std::pair<size_t, size_t>> starts;  // folded offset, source index
  for (size_t i = 0; i < haystack.size();) {
    const size_t width =
        i + 1 < haystack.size() && (haystack[i] & 0xFC00) == 0xD800 &&
                (haystack[i + 1] & 0xFC00) == 0xDC00
            ? 2
            : 1;
    starts.emplace_back(folded.size(), i);
    folded += base::i18n::FoldCase(haystack.substr(i, width));
    i += width;
  }
  const size_t found = folded.find(folded_needle);
  if (found == std::u16string::npos) {
    return false;
  }
  const size_t end = found + folded_needle.size();
  size_t from = haystack.size();
  size_t to = haystack.size();
  for (size_t k = 0; k < starts.size(); ++k) {
    if (starts[k].first <= found) {
      from = starts[k].second;
    }
    if (starts[k].first >= end) {
      to = starts[k].second;
      break;
    }
  }
  *index = from;
  *length = to - from;
  return *length > 0;
}

// `title` without the first match of `needle`, or `title` when it is not found.
std::string RemoveFirst(std::string_view title, std::string_view needle) {
  const std::u16string wide_title = base::UTF8ToUTF16(title);
  const std::u16string wide_needle = base::UTF8ToUTF16(
      base::TrimWhitespaceASCII(needle, base::TRIM_ALL));
  size_t index = 0;
  size_t length = 0;
  if (wide_needle.empty() ||
      !FindIgnoringCase(wide_needle, wide_title, &index, &length)) {
    return std::string(title);
  }
  std::u16string result = wide_title;
  result.erase(index, length);
  return base::UTF16ToUTF8(result);
}

std::string RemoveAll(std::string_view title, std::string_view needle) {
  std::string current(title);
  for (int i = 0; i < kMaxRemovals; ++i) {
    std::string next = RemoveFirst(current, needle);
    if (next == current) {
      break;
    }
    current = std::move(next);
  }
  return current;
}

std::string HostWithoutWww(const MatchInput& input) {
  std::string_view host = input.host();
  if (base::StartsWith(host, "www.")) {
    host.remove_prefix(4);
  }
  return std::string(host);
}

// Replaces "{title}" and "{host}" in `text`. Any other brace stays as typed.
std::string ExpandTemplate(std::string_view text,
                           std::string_view title_part,
                           std::string_view host) {
  std::string out;
  size_t i = 0;
  while (i < text.size()) {
    if (text[i] == '{') {
      if (text.substr(i).starts_with("{title}")) {
        out += title_part;
        i += 7;
        continue;
      }
      if (text.substr(i).starts_with("{host}")) {
        out += host;
        i += 6;
        continue;
      }
    }
    out += text[i];
    ++i;
  }
  return out;
}

std::string TrimAndCut(std::string_view text) {
  const std::u16string cut =
      CutToManualNameLength(base::UTF8ToUTF16(CleanSeparators(text)));
  return std::string(base::TrimWhitespaceASCII(base::UTF16ToUTF8(cut),
                                               base::TRIM_ALL));
}

}  // namespace

const char* RewriteModeToString(RewriteMode mode) {
  switch (mode) {
    case RewriteMode::kTemplate:
      return "template";
    case RewriteMode::kRemoveText:
      return "remove";
  }
  return "template";
}

bool RewriteModeFromString(std::string_view text, RewriteMode* mode) {
  for (RewriteMode candidate :
       {RewriteMode::kTemplate, RewriteMode::kRemoveText}) {
    if (text == RewriteModeToString(candidate)) {
      *mode = candidate;
      return true;
    }
  }
  return false;
}

bool IsValidRenameRuleId(std::string_view id) {
  return !id.empty() && id.size() <= 64 && OnlyRuleIdChars(id);
}

std::optional<RenameRule> RenameRuleFromValue(const base::DictValue& dict) {
  RenameRule rule;
  const std::string* id = dict.FindString("id");
  const std::string* name = dict.FindString("name");
  const std::string* type = dict.FindString("type");
  const std::string* value = dict.FindString("value");
  const std::string* mode = dict.FindString("mode");
  const std::string* text = dict.FindString("text");
  if (!id || !name || !type || !value || !mode || !text) {
    return std::nullopt;
  }
  if (!IsValidRenameRuleId(*id) || name->size() > kMaxRenameRuleNameBytes ||
      value->size() > kMaxRenameRuleTextBytes ||
      text->size() > kMaxRenameRuleTextBytes) {
    return std::nullopt;
  }
  if (!base::IsStringUTF8(*name) || !base::IsStringUTF8(*value) ||
      !base::IsStringUTF8(*text)) {
    return std::nullopt;
  }
  if (!RuleTypeFromString(*type, &rule.match.type) ||
      !RewriteModeFromString(*mode, &rule.mode)) {
    return std::nullopt;
  }
  // Absent counts as on; present must be a boolean.
  if (const base::Value* enabled = dict.Find("enabled")) {
    if (!enabled->is_bool()) {
      return std::nullopt;
    }
    rule.enabled = enabled->GetBool();
  }
  rule.id = *id;
  rule.name = std::string(base::TrimWhitespaceASCII(*name, base::TRIM_ALL));
  rule.match.id = *id;
  rule.match.enabled = true;
  rule.match.value =
      rule.match.type == RuleType::kDomain
          ? NormalizeDomain(*value)
          : std::string(base::TrimWhitespaceASCII(*value, base::TRIM_ALL));
  if (rule.match.value.empty()) {
    return std::nullopt;
  }
  rule.text = *text;
  // A template with nothing in it would rename every tab to nothing.
  if (rule.mode == RewriteMode::kTemplate &&
      base::TrimWhitespaceASCII(rule.text, base::TRIM_ALL).empty()) {
    return std::nullopt;
  }
  // "Remove text" has to know what to remove.
  if (rule.mode == RewriteMode::kRemoveText &&
      base::TrimWhitespaceASCII(rule.text, base::TRIM_ALL).empty() &&
      rule.match.type != RuleType::kTitleContains) {
    return std::nullopt;
  }
  return rule;
}

base::DictValue RenameRuleToValue(const RenameRule& rule) {
  base::DictValue dict;
  dict.Set("id", rule.id);
  dict.Set("name", rule.name);
  dict.Set("type", RuleTypeToString(rule.match.type));
  dict.Set("value", rule.match.value);
  dict.Set("mode", RewriteModeToString(rule.mode));
  dict.Set("text", rule.text);
  dict.Set("enabled", rule.enabled);
  return dict;
}

std::vector<RenameRule> RenameRulesFromList(const base::ListValue& list) {
  std::vector<RenameRule> rules;
  std::set<std::string> seen;
  for (const base::Value& entry : list) {
    if (rules.size() >= kMaxRenameRules) {
      break;
    }
    if (!entry.is_dict()) {
      continue;
    }
    std::optional<RenameRule> rule = RenameRuleFromValue(entry.GetDict());
    if (!rule || !seen.insert(rule->id).second) {
      continue;
    }
    rules.push_back(std::move(*rule));
  }
  return rules;
}

base::ListValue RenameRulesToList(const std::vector<RenameRule>& rules) {
  base::ListValue list;
  for (const RenameRule& rule : rules) {
    list.Append(RenameRuleToValue(rule));
  }
  return list;
}

std::optional<std::string> ApplyRenameRule(const RenameRule& rule,
                                           const MatchInput& input,
                                           std::string_view title) {
  if (!Matches(rule.match, input)) {
    return std::nullopt;
  }
  std::string result;
  switch (rule.mode) {
    case RewriteMode::kRemoveText: {
      const std::string_view needle =
          base::TrimWhitespaceASCII(rule.text, base::TRIM_ALL).empty()
              ? std::string_view(rule.match.value)
              : std::string_view(rule.text);
      result = TrimAndCut(RemoveAll(title, needle));
      break;
    }
    case RewriteMode::kTemplate: {
      // {title} loses the matched text when the rule matched the title.
      const std::string title_part =
          rule.match.type == RuleType::kTitleContains
              ? CleanSeparators(RemoveFirst(title, rule.match.value))
              : CleanSeparators(title);
      result = TrimAndCut(
          ExpandTemplate(rule.text, title_part, HostWithoutWww(input)));
      break;
    }
  }
  if (result.empty() || result == title) {
    return std::nullopt;
  }
  return result;
}

std::optional<RenameOutcome> ApplyRenameRules(const RenameRules& rules,
                                              const MatchInput& input,
                                              std::string_view title) {
  if (!rules.enabled) {
    return std::nullopt;
  }
  for (const RenameRule& rule : rules.rules) {
    if (!rule.enabled) {
      continue;
    }
    if (std::optional<std::string> text = ApplyRenameRule(rule, input, title)) {
      return RenameOutcome{std::move(*text), &rule};
    }
  }
  return std::nullopt;
}

}  // namespace aerie_tab_model
