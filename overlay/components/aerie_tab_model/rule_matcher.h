// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_TAB_MODEL_RULE_MATCHER_H_
#define COMPONENTS_AERIE_TAB_MODEL_RULE_MATCHER_H_

#include <string>
#include <string_view>
#include <vector>

class GURL;

// The rule grammar of auto-filing folders (issue #139, docs/sidebar-design.md,
// "Rule grammar"). Smart folders (#140), the rename rules (#146) and Workspace
// link routing (#172) share it, so this file is small on purpose: four rule
// types, no regular expressions, no profile, no tab and no Views. It is plain
// code with unit tests.
namespace aerie_tab_model {

enum class RuleType {
  // The host, or any subdomain of it: `example.com` matches `example.com` and
  // `news.example.com`, not `badexample.com`.
  kDomain,
  // The address starts with the text. The scheme is not compared when it is
  // http or https (and the rule may leave it out), the host is not case
  // sensitive, and a trailing slash does not count.
  kUrlStartsWith,
  // The address contains the text, in any case.
  kUrlContains,
  // The page title contains the text, in any case.
  kTitleContains,
};

// The names are persisted values; do not rename them.
const char* RuleTypeToString(RuleType type);
bool RuleTypeFromString(std::string_view text, RuleType* type);

struct Rule {
  // Stable. Recorded in a tab's `filed_by`.
  std::string id;
  RuleType type = RuleType::kDomain;
  std::string value;
  // The rule's own switch.
  bool enabled = true;

  bool operator==(const Rule&) const = default;
};

// The rules of one folder. `enabled` is the folder's "All" switch.
struct FolderRules {
  bool enabled = true;
  std::vector<Rule> rules;
  // "Don't file this site": a host (and its subdomains) that these rules never
  // match.
  std::vector<std::string> excluded_hosts;
  // "Stop this rule matching this tab": record ids of tabs that these rules
  // never match.
  std::vector<std::string> excluded_tabs;

  bool operator==(const FolderRules&) const = default;
};

// True when `rule` matches a page with this address and title, whatever its own
// switch says. An empty rule value never matches. `title` may be empty (the
// page has not given one yet).
bool Matches(const Rule& rule, const GURL& url, std::string_view title);

// The canonical host that a Domain rule value stands for (lower case, a pasted
// address cut to its host, no `*.`, no trailing dot), or an empty string when
// nothing usable is left. What the editor stores for a Domain rule or a
// "Don't file this site" exception.
std::string NormalizeDomain(std::string_view text);

// True when `host` is `domain` or a subdomain of it. Both are normalised first
// (case, a leading `*.` or `.`, a trailing dot). `badexample.com` is not a
// subdomain of `example.com`.
bool HostMatchesDomain(std::string_view host, std::string_view domain);

// The first enabled rule of `rules` that matches, or null. Null as well when
// the "All" switch is off, when the host of `url` is excluded, or when
// `tab_id` is excluded.
const Rule* FindMatchingRule(const FolderRules& rules,
                             const GURL& url,
                             std::string_view title,
                             std::string_view tab_id);

}  // namespace aerie_tab_model

#endif  // COMPONENTS_AERIE_TAB_MODEL_RULE_MATCHER_H_
