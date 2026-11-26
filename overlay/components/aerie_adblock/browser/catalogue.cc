// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_adblock/browser/catalogue.h"

#include <array>

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

}  // namespace aerie_adblock
