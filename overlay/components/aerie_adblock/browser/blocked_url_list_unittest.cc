// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_adblock/browser/blocked_url_list.h"

#include <string>

#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace aerie_adblock {
namespace {

TEST(BlockedUrlListTest, StartsEmptyAndKeepsUrlsInOrder) {
  BlockedUrlList list;
  EXPECT_TRUE(list.urls().empty());
  list.Add(GURL("https://ads.test/a.js"));
  list.Add(GURL("https://ads.test/b.js"));
  EXPECT_EQ((std::vector<std::string>{"https://ads.test/a.js",
                                      "https://ads.test/b.js"}),
            list.urls());
}

TEST(BlockedUrlListTest, HoldsAtMostTwoHundredEntriesAndKeepsTheFirst) {
  BlockedUrlList list;
  for (int i = 0; i < 300; ++i) {
    list.Add(GURL(base::StrCat({"https://ads.test/", base::NumberToString(i)})));
  }
  ASSERT_EQ(200u, list.urls().size());
  EXPECT_EQ("https://ads.test/0", list.urls().front());
  EXPECT_EQ("https://ads.test/199", list.urls().back());
}

TEST(BlockedUrlListTest, CutsALongUrlTo256Characters) {
  BlockedUrlList list;
  const std::string long_url =
      base::StrCat({"https://ads.test/", std::string(2000, 'a')});
  list.Add(GURL(long_url));
  list.Add(GURL("https://ads.test/short"));
  ASSERT_EQ(2u, list.urls().size());
  EXPECT_EQ(256u, list.urls()[0].size());
  EXPECT_EQ(long_url.substr(0, 256), list.urls()[0]);
  EXPECT_EQ("https://ads.test/short", list.urls()[1]);
}

TEST(BlockedUrlListTest, ANonAsciiUrlIsEscapedSoTheCutKeepsValidText) {
  BlockedUrlList list;
  list.Add(GURL(base::StrCat({"https://ads.test/", std::string(300, '\xc3')})));
  for (char c : list.urls()[0]) {
    EXPECT_GE(static_cast<unsigned char>(c), 0x20);
    EXPECT_LT(static_cast<unsigned char>(c), 0x7f);
  }
}

TEST(BlockedUrlListTest, ClearEmptiesTheListAndItFillsAgain) {
  BlockedUrlList list;
  for (int i = 0; i < 250; ++i) {
    list.Add(GURL("https://ads.test/x"));
  }
  list.Clear();
  EXPECT_TRUE(list.urls().empty());
  list.Add(GURL("https://ads.test/y"));
  EXPECT_EQ(1u, list.urls().size());
}

}  // namespace
}  // namespace aerie_adblock
