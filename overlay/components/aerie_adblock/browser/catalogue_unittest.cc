// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_adblock/browser/catalogue.h"

#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include "components/aerie_adblock/browser/filter_list_state.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace aerie_adblock {
namespace {

TEST(CatalogueTest, AKnownIdResolvesToItsBuildTimeAddress) {
  const CatalogueEntry* entry = FindCatalogueEntry("fanboyannoyances");
  ASSERT_TRUE(entry);
  EXPECT_EQ("https://easylist.to/easylist/fanboy-annoyance.txt", entry->url);
  EXPECT_EQ("annoyances", entry->category);

  entry = FindCatalogueEntry("easylistgermany");
  ASSERT_TRUE(entry);
  EXPECT_EQ("https://easylist.to/easylistgermany/easylistgermany.txt",
            entry->url);
  EXPECT_EQ("regional", entry->category);
  EXPECT_EQ("de", entry->langs);
}

TEST(CatalogueTest, AnUnknownIdIsRefused) {
  for (const char* id : {"", "nope", "easylist", "easyprivacy", "custom",
                         "FANBOYANNOYANCES", "fanboyannoyances ",
                         " fanboyannoyances", "fanboyannoyance", "urlhaus"}) {
    EXPECT_EQ(nullptr, FindCatalogueEntry(id)) << id;
  }
}

TEST(CatalogueTest, ARawAddressIsNotAnId) {
  for (const char* raw :
       {"https://easylist.to/easylist/fanboy-annoyance.txt",
        "https://evil.example/list.txt", "http://127.0.0.1:8000/list.txt",
        "file:///etc/passwd", "easylist.to/easylist/fanboy-annoyance.txt",
        "//evil.example/list.txt"}) {
    EXPECT_EQ(nullptr, FindCatalogueEntry(raw)) << raw;
  }
  // Not even the address of an entry that is in the table.
  for (const CatalogueEntry& entry : Catalogue()) {
    EXPECT_EQ(nullptr, FindCatalogueEntry(entry.url)) << entry.url;
  }
}

TEST(CatalogueTest, EveryEntryIsUsableAsAListAndComesFromAnHttpsPublisher) {
  ASSERT_FALSE(Catalogue().empty());
  std::set<std::string_view> ids;
  for (const CatalogueEntry& entry : Catalogue()) {
    EXPECT_TRUE(IsValidListId(entry.id)) << entry.id;
    EXPECT_TRUE(ids.insert(entry.id).second) << "duplicate " << entry.id;
    // The same check the fetcher makes, with no loopback exception.
    EXPECT_TRUE(IsAcceptableListUrl(GURL(entry.url), false)) << entry.id;
    EXPECT_TRUE(GURL(entry.url).SchemeIs("https")) << entry.id;
    EXPECT_TRUE(GURL(entry.homepage).SchemeIs("https")) << entry.id;
    EXPECT_FALSE(entry.licence.empty()) << entry.id;
    // Never the id of a built-in list or of the user's own rules, nor the id a
    // custom list at some address would get.
    EXPECT_NE(kEasyListId, entry.id);
    EXPECT_NE(kEasyPrivacyId, entry.id);
    EXPECT_NE("custom", entry.id);
    const auto& categories = CatalogueCategories();
    EXPECT_NE(categories.end(),
              std::find(categories.begin(), categories.end(), entry.category))
        << entry.id;
  }
}

TEST(CatalogueTest, TheCategoriesAreInTheOrderOfThePage) {
  std::vector<std::string_view> names(CatalogueCategories().begin(),
                                      CatalogueCategories().end());
  EXPECT_EQ((std::vector<std::string_view>{"annoyances", "cookies", "social",
                                           "privacy", "regional"}),
            names);
}

}  // namespace
}  // namespace aerie_adblock
