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

// The suggestions (issue #106) are a pure function of the locale, the language
// list and a table. This table is the test's own, so the tests do not move when
// the catalogue does.
constexpr CatalogueEntry kTable[] = {
    {"alpha", "https://a.example/a.txt", "https://a.example/", "GPL-3.0",
     "annoyances", ""},
    {"germany", "https://a.example/de.txt", "https://a.example/", "GPL-3.0",
     "regional", "de"},
    {"iberia", "https://a.example/es.txt", "https://a.example/", "GPL-3.0",
     "regional", "es,pt"},
    {"france", "https://a.example/fr.txt", "https://a.example/", "GPL-3.0",
     "regional", "fr"},
    {"austria", "https://a.example/de2.txt", "https://a.example/", "GPL-3.0",
     "regional", "de,en"},
};

using Ids = std::vector<std::string>;

Ids Suggest(std::string_view locale, std::string_view accept) {
  return SuggestCatalogueIds(locale, accept, kTable);
}

TEST(CatalogueSuggestionTest, AGermanLocaleSuggestsTheGermanLists) {
  EXPECT_EQ((Ids{"germany", "austria"}), Suggest("de", ""));
  EXPECT_EQ((Ids{"germany", "austria"}), Suggest("de-DE", "de-DE,de"));
}

TEST(CatalogueSuggestionTest, OnlyTheLanguageSubtagCounts) {
  EXPECT_EQ((Ids{"iberia"}), Suggest("pt-BR", ""));
  EXPECT_EQ((Ids{"iberia"}), Suggest("", "pt-PT"));
  EXPECT_EQ((Ids{"iberia"}), Suggest("es_MX", ""));
}

TEST(CatalogueSuggestionTest, EnglishAloneSuggestsNoRegionalList) {
  EXPECT_EQ(Ids{}, SuggestCatalogueIds("en-US", "en-US,en", Catalogue()));
  // A list that carries "en" among its languages is still a match.
  EXPECT_EQ((Ids{"austria"}), Suggest("en-US", "en"));
}

TEST(CatalogueSuggestionTest, AWeightedListIsReadTheWayAnAcceptLanguageIs) {
  EXPECT_EQ((Ids{"germany", "france", "austria"}),
            Suggest("en-US", "fr,de;q=0.8"));
  EXPECT_EQ((Ids{"germany", "france", "austria"}),
            Suggest("en-US", "fr;q=0.9 , de;q=0.8, en;q=0.1"));
  // The weight is not a language, and q=0 does not hide a language: the pref
  // is a list the user chose, not a negotiation.
  EXPECT_EQ(Ids{}, Suggest("", "q=0.8"));
}

TEST(CatalogueSuggestionTest, CaseDoesNotMatter) {
  EXPECT_EQ((Ids{"germany", "austria"}), Suggest("DE-de", ""));
  EXPECT_EQ((Ids{"france"}), Suggest("", "FR-ca"));
}

TEST(CatalogueSuggestionTest, EachListIsSuggestedOnceInTheOrderOfTheTable) {
  // Three inputs name German and one names Spanish, in any order.
  EXPECT_EQ((Ids{"germany", "iberia", "austria"}),
            Suggest("de-AT", "pt,de,de-CH,es"));
  EXPECT_EQ(Suggest("de", "es"), Suggest("es", "de"));
}

TEST(CatalogueSuggestionTest, MalformedOrEmptyInputSuggestsNothing) {
  for (const char* bad : {"", " ", ",", ";;", "-", "_", "1", "d", "deutsch",
                          "d3", "--", ";q=1", "\u0000"}) {
    EXPECT_EQ(Ids{}, Suggest(bad, bad)) << bad;
  }
  EXPECT_EQ(Ids{}, Suggest("", ""));
  EXPECT_EQ(Ids{}, SuggestCatalogueIds("de", "de", {}));
  // A bad entry among good ones does not hide the good ones.
  EXPECT_EQ((Ids{"france"}), Suggest("", ",,;;,fr,-,xx1"));
  // A very long input is not an unbounded loop.
  EXPECT_EQ(Ids{}, Suggest(std::string(100000, ','), std::string(100000, 'x')));
}

TEST(CatalogueSuggestionTest, NoListWithoutLanguagesIsEverSuggested) {
  for (std::string_view tag : {"", "en", "de", "fr", "es", "pt", "ja", "it"}) {
    for (const std::string& id : Suggest(tag, tag)) {
      EXPECT_NE("alpha", id);
    }
  }
}

TEST(CatalogueSuggestionTest, TheRealCatalogueSuggestsItsRegionalLists) {
  auto real = [](std::string_view locale, std::string_view accept) {
    return SuggestCatalogueIds(locale, accept, Catalogue());
  };
  EXPECT_EQ((Ids{"easylistgermany"}), real("de", "de"));
  EXPECT_EQ(Ids{}, real("en-US", "en-US,en"));
  for (const std::string& id : real("de", "fr,it,ja,es,pt")) {
    const CatalogueEntry* entry = FindCatalogueEntry(id);
    ASSERT_TRUE(entry);
    EXPECT_FALSE(entry->langs.empty());
    EXPECT_EQ("regional", entry->category);
  }
}

}  // namespace
}  // namespace aerie_adblock
