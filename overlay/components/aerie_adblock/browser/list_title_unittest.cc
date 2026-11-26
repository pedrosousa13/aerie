// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_adblock/browser/list_title.h"

#include <string>

#include "testing/gtest/include/gtest/gtest.h"

namespace aerie_adblock {
namespace {

TEST(ListTitleTest, ReadsTheTitleLineOfTheHeader) {
  EXPECT_EQ("My list",
            ParseListTitle("[Adblock Plus 2.0]\n! Title: My list\n"
                           "||ads.example^\n"));
}

TEST(ListTitleTest, TheKeyIsCaseInsensitiveAndTheValueIsTrimmed) {
  EXPECT_EQ("Spaced out", ParseListTitle("!   title:   Spaced out \t\r\n"));
}

TEST(ListTitleTest, NoTitleGivesAnEmptyString) {
  EXPECT_EQ("", ParseListTitle("! Homepage: https://x.example\n||a.example^\n"));
  EXPECT_EQ("", ParseListTitle(""));
  EXPECT_EQ("", ParseListTitle("! Title:\n||a.example^\n"));
  EXPECT_EQ("", ParseListTitle("! Title:   \t \n"));
}

TEST(ListTitleTest, ATitleAfterTheHeaderIsIgnored) {
  // The header ends at the first rule line; a "Title" comment further down
  // is list content, not the list's name.
  EXPECT_EQ("", ParseListTitle("||a.example^\n! Title: Late\n"));
  std::string long_header;
  for (int i = 0; i < 200; ++i) {
    long_header += "! comment\n";
  }
  EXPECT_EQ("", ParseListTitle(long_header + "! Title: Far below\n"));
}

TEST(ListTitleTest, HtmlStaysText) {
  // The page shows it as text; the parser does not rewrite it.
  EXPECT_EQ("<b>Bold</b><script>x()</script>",
            ParseListTitle("! Title: <b>Bold</b><script>x()</script>\n"));
}

TEST(ListTitleTest, AVeryLongTitleIsCut) {
  std::string title(5000, 'a');
  std::string out = ParseListTitle("! Title: " + title + "\n");
  EXPECT_EQ(kMaxListTitleBytes, out.size());
  EXPECT_EQ(std::string(kMaxListTitleBytes, 'a'), out);
}

TEST(ListTitleTest, ACutNeverSplitsACharacter) {
  // Each "é" is two bytes; an odd limit must not leave half of one.
  std::string title;
  for (size_t i = 0; i < kMaxListTitleBytes; ++i) {
    title += "\xC3\xA9";
  }
  std::string out = ParseListTitle("! Title: " + title + "\n");
  EXPECT_LE(out.size(), kMaxListTitleBytes);
  ASSERT_FALSE(out.empty());
  EXPECT_EQ(0u, out.size() % 2);
}

TEST(ListTitleTest, ControlCharactersAreRemoved) {
  EXPECT_EQ("a[31mb", ParseListTitle("! Title: a\x01\x1b[31m\x07" "b\n"));
  EXPECT_EQ("a b", ParseListTitle("! Title: a\x7f b\n"));
  EXPECT_EQ("tab", ParseListTitle("! Title: t\tab\n"));
  EXPECT_EQ("", ParseListTitle("! Title: \x01\x02\n"));
}

TEST(ListTitleTest, OnlyTheHeaderIsRead) {
  // A huge first line is not scanned past the byte bound.
  std::string text = "! Title: ok\n" + std::string(4 * 1024 * 1024, 'x');
  EXPECT_EQ("ok", ParseListTitle(text));
}

}  // namespace
}  // namespace aerie_adblock
