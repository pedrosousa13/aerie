// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_adblock/browser/catalogue.h"

#include <array>
#include <set>

#include "base/strings/string_split.h"
#include "base/strings/string_util.h"

namespace aerie_adblock {

base::span<const CatalogueEntry> Catalogue() {
  return base::span(kCatalogueTable, kCatalogueTableSize);
}

const CatalogueEntry* FindCatalogueEntry(std::string_view id) {
  for (const CatalogueEntry& entry : Catalogue()) {
    if (entry.id == id) {
      return &entry;
    }
  }
  return nullptr;
}

base::span<const std::string_view> CatalogueCategories() {
  static constexpr std::array<std::string_view, 5> kCategories = {
      "annoyances", "cookies", "social", "privacy", "regional"};
  return kCategories;
}

namespace {

// A language subtag: two or three ASCII letters.
bool IsLanguageSubtag(std::string_view text) {
  if (text.size() < 2 || text.size() > 3) {
    return false;
  }
  for (char c : text) {
    if (!base::IsAsciiAlpha(c)) {
      return false;
    }
  }
  return true;
}

// Adds the language subtag of every tag of a comma separated list to |out|.
void CollectLanguages(std::string_view list, std::set<std::string>& out) {
  for (std::string_view part : base::SplitStringPiece(
           list, ",", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
    // "fr;q=0.8": the weight is not part of the tag.
    part = base::TrimWhitespaceASCII(part.substr(0, part.find(';')),
                                     base::TRIM_ALL);
    std::string_view subtag = part.substr(0, part.find_first_of("-_"));
    if (IsLanguageSubtag(subtag)) {
      out.insert(base::ToLowerASCII(subtag));
    }
  }
}

}  // namespace

std::vector<std::string> SuggestCatalogueIds(
    std::string_view app_locale,
    std::string_view accept_languages,
    base::span<const CatalogueEntry> table) {
  std::set<std::string> wanted;
  CollectLanguages(app_locale, wanted);
  CollectLanguages(accept_languages, wanted);
  std::vector<std::string> out;
  std::set<std::string_view> seen;
  for (const CatalogueEntry& entry : table) {
    if (!seen.insert(entry.id).second) {
      continue;
    }
    for (std::string_view lang : base::SplitStringPiece(
             entry.langs, ",", base::TRIM_WHITESPACE,
             base::SPLIT_WANT_NONEMPTY)) {
      if (wanted.contains(base::ToLowerASCII(lang))) {
        out.emplace_back(entry.id);
        break;
      }
    }
  }
  return out;
}

}  // namespace aerie_adblock
