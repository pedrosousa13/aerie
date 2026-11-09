// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_fonts/font_allowlist.h"

#include <cstddef>
#include <string>

namespace aerie_fonts {
namespace {

// The approved list: components/aerie_fonts/allowed_font_families.inc.
constexpr std::string_view kAllowedFamilies[] = {
#include "components/aerie_fonts/allowed_font_families.inc"
};

// Words that can follow a family name in a full name or a PostScript name.
constexpr std::string_view kStyleWords[] = {
    "regular",   "roman",     "normal",    "book",       "bold",
    "italic",    "oblique",   "light",     "medium",     "thin",
    "black",     "heavy",     "condensed", "semibold",   "demibold",
    "extrabold", "extralight", "semilight",
};

char Lower(char c) {
  return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

bool EqualsIgnoreCase(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (size_t i = 0; i < a.size(); ++i) {
    if (Lower(a[i]) != Lower(b[i])) {
      return false;
    }
  }
  return true;
}

std::string WithoutSpaces(std::string_view s) {
  std::string out;
  for (char c : s) {
    if (c != ' ') {
      out.push_back(c);
    }
  }
  return out;
}

// Whether `word` is one style word or several run together, as a PostScript
// name writes them ("BoldOblique").
bool IsStyleWord(std::string_view word) {
  if (word.empty()) {
    return false;
  }
  for (std::string_view style : kStyleWords) {
    if (word.size() < style.size() ||
        !EqualsIgnoreCase(word.substr(0, style.size()), style)) {
      continue;
    }
    std::string_view rest = word.substr(style.size());
    if (rest.empty() || IsStyleWord(rest)) {
      return true;
    }
  }
  return false;
}

// Whether `rest` is a space or a dash followed by style words only, such as
// " Bold" or "-BoldOblique". Words are separated by a space or a dash.
bool IsStyleSuffix(std::string_view rest) {
  if (rest.empty() || (rest[0] != ' ' && rest[0] != '-')) {
    return false;
  }
  size_t pos = 1;
  while (pos <= rest.size()) {
    size_t end = rest.find_first_of(" -", pos);
    if (end == std::string_view::npos) {
      end = rest.size();
    }
    if (end == pos || !IsStyleWord(rest.substr(pos, end - pos))) {
      return false;
    }
    pos = end + 1;
  }
  return true;
}

// Whether `name` is `prefix` followed by a style suffix.
bool IsPrefixedByStyleSuffix(std::string_view name, std::string_view prefix) {
  return name.size() > prefix.size() &&
         EqualsIgnoreCase(name.substr(0, prefix.size()), prefix) &&
         IsStyleSuffix(name.substr(prefix.size()));
}

}  // namespace

bool IsFontFamilyAllowed(std::string_view family) {
  for (std::string_view allowed : kAllowedFamilies) {
    if (EqualsIgnoreCase(family, allowed)) {
      return true;
    }
  }
  return false;
}

bool IsLocalFontNameAllowed(std::string_view name) {
  if (IsFontFamilyAllowed(name)) {
    return true;
  }
  for (std::string_view allowed : kAllowedFamilies) {
    if (IsPrefixedByStyleSuffix(name, allowed)) {
      return true;
    }
    // A PostScript name has no spaces: "DejaVuSans" or "DejaVuSans-Bold".
    const std::string joined = WithoutSpaces(allowed);
    if (EqualsIgnoreCase(name, joined) ||
        IsPrefixedByStyleSuffix(name, joined)) {
      return true;
    }
  }
  return false;
}

bool ShouldRestrictFonts(const Policy& policy) {
  return policy.master_enabled && policy.surface_enabled && policy.has_token;
}

}  // namespace aerie_fonts
