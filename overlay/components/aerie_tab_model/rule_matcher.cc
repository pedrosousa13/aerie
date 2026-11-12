// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_tab_model/rule_matcher.h"

#include <string>

#include "base/i18n/case_conversion.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "url/gurl.h"

namespace aerie_tab_model {

namespace {

// The text with the case folded, for the comparisons that ignore case.
std::string Fold(std::string_view text) {
  return base::UTF16ToUTF8(base::i18n::FoldCase(base::UTF8ToUTF16(text)));
}

std::string_view StripTrailingSlashes(std::string_view text) {
  while (!text.empty() && text.back() == '/') {
    text.remove_suffix(1);
  }
  return text;
}

// An address without an http or https scheme, folded. Another scheme (chrome,
// file, ftp) stays, so a rule for it only matches that scheme.
std::string SchemeLess(std::string_view address) {
  std::string folded = Fold(base::TrimWhitespaceASCII(address, base::TRIM_ALL));
  for (std::string_view scheme : {"https://", "http://"}) {
    if (base::StartsWith(folded, scheme)) {
      return folded.substr(scheme.size());
    }
  }
  return folded;
}

bool UrlStartsWith(const GURL& url, std::string_view value) {
  std::string rule = SchemeLess(value);
  rule = std::string(StripTrailingSlashes(rule));
  if (rule.empty()) {
    return false;
  }
  const std::string spec = SchemeLess(url.spec());
  if (!base::StartsWith(spec, rule)) {
    return false;
  }
  // A rule that names only a site ("example.com") ends at the host: it must not
  // match "example.com.evil.test". Anything with a path is a plain prefix.
  if (rule.find('/') == std::string::npos &&
      rule.find('?') == std::string::npos &&
      rule.find('#') == std::string::npos) {
    if (spec.size() == rule.size()) {
      return true;
    }
    const char next = spec[rule.size()];
    return next == '/' || next == '?' || next == '#' ||
           (next == ':' && rule.find(':') == std::string::npos);
  }
  return true;
}

bool UrlContains(const GURL& url, std::string_view value) {
  const std::string needle =
      Fold(base::TrimWhitespaceASCII(value, base::TRIM_ALL));
  return !needle.empty() && Fold(url.spec()).find(needle) != std::string::npos;
}

bool TitleContains(std::string_view title, std::string_view value) {
  const std::string needle =
      Fold(base::TrimWhitespaceASCII(value, base::TRIM_ALL));
  return !needle.empty() && !title.empty() &&
         Fold(title).find(needle) != std::string::npos;
}

}  // namespace

// What a rule says a domain is, as a canonical host: lower case, no scheme, no
// path, no port, no leading `*.` or `.`, no trailing dot. Empty when nothing
// usable is left.
std::string NormalizeDomain(std::string_view text) {
  std::string_view trimmed = base::TrimWhitespaceASCII(text, base::TRIM_ALL);
  if (trimmed.empty()) {
    return std::string();
  }
  std::string_view bare = trimmed;
  if (base::StartsWith(bare, "*.")) {
    bare.remove_prefix(2);
  }
  while (!bare.empty() && bare.front() == '.') {
    bare.remove_prefix(1);
  }
  // GURL makes the canonical host (lower case, IDN as punycode) and drops a
  // scheme, path or port that a user pasted with it.
  std::string with_scheme = bare.find("://") == std::string_view::npos
                                ? "http://" + std::string(bare)
                                : std::string(bare);
  GURL url(with_scheme);
  if (!url.is_valid() || !url.has_host()) {
    return std::string();
  }
  std::string host(url.host());
  while (!host.empty() && host.back() == '.') {
    host.pop_back();
  }
  return host;
}

const char* RuleTypeToString(RuleType type) {
  switch (type) {
    case RuleType::kDomain:
      return "domain";
    case RuleType::kUrlStartsWith:
      return "url-starts-with";
    case RuleType::kUrlContains:
      return "url-contains";
    case RuleType::kTitleContains:
      return "title-contains";
  }
  return "domain";
}

bool RuleTypeFromString(std::string_view text, RuleType* type) {
  for (RuleType candidate :
       {RuleType::kDomain, RuleType::kUrlStartsWith, RuleType::kUrlContains,
        RuleType::kTitleContains}) {
    if (text == RuleTypeToString(candidate)) {
      *type = candidate;
      return true;
    }
  }
  return false;
}

bool HostMatchesDomain(std::string_view host, std::string_view domain) {
  const std::string want = NormalizeDomain(domain);
  std::string have = NormalizeDomain(host);
  if (want.empty() || have.empty()) {
    return false;
  }
  if (have == want) {
    return true;
  }
  return have.size() > want.size() && base::EndsWith(have, want) &&
         have[have.size() - want.size() - 1] == '.';
}

bool Matches(const Rule& rule, const GURL& url, std::string_view title) {
  switch (rule.type) {
    case RuleType::kDomain:
      return url.is_valid() && url.has_host() &&
             HostMatchesDomain(url.host(), rule.value);
    case RuleType::kUrlStartsWith:
      return url.is_valid() && UrlStartsWith(url, rule.value);
    case RuleType::kUrlContains:
      return url.is_valid() && UrlContains(url, rule.value);
    case RuleType::kTitleContains:
      return TitleContains(title, rule.value);
  }
  return false;
}

const Rule* FindMatchingRule(const FolderRules& rules,
                             const GURL& url,
                             std::string_view title,
                             std::string_view tab_id) {
  if (!rules.enabled) {
    return nullptr;
  }
  for (const std::string& id : rules.excluded_tabs) {
    if (!tab_id.empty() && id == tab_id) {
      return nullptr;
    }
  }
  if (url.is_valid() && url.has_host()) {
    for (const std::string& host : rules.excluded_hosts) {
      if (HostMatchesDomain(url.host(), host)) {
        return nullptr;
      }
    }
  }
  for (const Rule& rule : rules.rules) {
    if (rule.enabled && Matches(rule, url, title)) {
      return &rule;
    }
  }
  return nullptr;
}

}  // namespace aerie_tab_model
