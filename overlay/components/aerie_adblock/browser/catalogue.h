// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_ADBLOCK_BROWSER_CATALOGUE_H_
#define COMPONENTS_AERIE_ADBLOCK_BROWSER_CATALOGUE_H_

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

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

// The regional lists to suggest for a language setting (issue #106). Pure: it
// reads no pref, no global and no network, so the same three inputs always give
// the same answer.
//
// |app_locale| is the browser's locale ("de", "pt-BR"). |accept_languages| is
// the text of the "intl.accept_languages" pref: a comma separated list of
// language tags, each with an optional ";q=" weight ("fr,de;q=0.8"). Only the
// language subtag of each counts ("pt-BR" is "pt") and case does not matter.
// A part that is not a language tag is skipped.
//
// Returns the id of every entry of |table| whose |langs| names one of those
// languages, once each, in the order of |table|. Empty input gives an empty
// result. An entry with no |langs| is never suggested. Nothing is turned on and
// nothing is fetched by asking.
std::vector<std::string> SuggestCatalogueIds(
    std::string_view app_locale,
    std::string_view accept_languages,
    base::span<const CatalogueEntry> table);

}  // namespace aerie_adblock

#endif  // COMPONENTS_AERIE_ADBLOCK_BROWSER_CATALOGUE_H_
