// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_ADBLOCK_BROWSER_CATALOGUE_H_
#define COMPONENTS_AERIE_ADBLOCK_BROWSER_CATALOGUE_H_

#include <cstddef>
#include <string_view>

#include "base/containers/span.h"

namespace aerie_adblock {

// The filter list catalogue (issue #105, ADR 0003): the fixed set of publisher
// addresses the browser may download a list from when the user turns that list
// on. The table is generated at build time from
// components/aerie_adblock/catalogue.json (generate_catalogue.py), so the
// addresses are part of the build. Nothing else names a catalogue address: the
// Settings page and the handler pass an id, and an id that is not in the table
// is refused. A raw address is not an id.
//
// The words the user sees (title, description, category) are not here: they
// are strings of the grd, looked up by id on the page.
struct CatalogueEntry {
  std::string_view id;        // [a-z0-9]{1,32}, also the list id in the state.
  std::string_view url;       // https, from the publisher.
  std::string_view homepage;  // https.
  std::string_view licence;   // On the allow-list of generate_catalogue.py.
  std::string_view category;  // One of CatalogueCategories().
  std::string_view langs;     // Comma separated language codes; "" for none.
};

// The generated table (catalogue_table.cc).
extern const CatalogueEntry kCatalogueTable[];
extern const size_t kCatalogueTableSize;

// Every entry, in the order of catalogue.json.
base::span<const CatalogueEntry> Catalogue();

// The entry whose id is exactly |id|, or null. An address, a title or an id in
// another case does not match.
const CatalogueEntry* FindCatalogueEntry(std::string_view id);

// The categories of the page, in the order it shows them.
base::span<const std::string_view> CatalogueCategories();

}  // namespace aerie_adblock

#endif  // COMPONENTS_AERIE_ADBLOCK_BROWSER_CATALOGUE_H_
