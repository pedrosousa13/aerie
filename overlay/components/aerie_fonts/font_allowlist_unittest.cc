// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_fonts/font_allowlist.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace aerie_fonts {
namespace {

TEST(FontAllowlistTest, TheApprovedFamiliesAreAllowed) {
  for (const char* family :
       {"DejaVu Sans", "DejaVu Serif", "DejaVu Sans Mono", "Liberation Sans",
        "Liberation Serif", "Liberation Mono", "Noto Sans", "Noto Serif",
        "Noto Sans Mono", "Noto Sans CJK SC", "Noto Sans CJK TC",
        "Noto Sans CJK JP", "Noto Sans CJK KR", "Noto Serif CJK SC",
        "Noto Serif CJK TC", "Noto Serif CJK JP", "Noto Serif CJK KR",
        "Noto Color Emoji", "Cantarell", "Ubuntu", "Ubuntu Mono", "FreeSans",
        "FreeSerif", "FreeMono", "Droid Sans", "Droid Serif",
        "Droid Sans Mono"}) {
    EXPECT_TRUE(IsFontFamilyAllowed(family)) << family;
  }
}

TEST(FontAllowlistTest, TheMicrosoftAliasesAreAllowed) {
  for (const char* family : {"Arial", "Helvetica", "Times New Roman", "Times",
                             "Courier New", "Courier"}) {
    EXPECT_TRUE(IsFontFamilyAllowed(family)) << family;
  }
}

TEST(FontAllowlistTest, TheCssGenericsAreAllowed) {
  for (const char* family :
       {"serif", "sans-serif", "monospace", "cursive", "fantasy", "system-ui",
        "ui-serif", "ui-sans-serif", "ui-monospace", "ui-rounded", "emoji",
        "math", "fangsong"}) {
    EXPECT_TRUE(IsFontFamilyAllowed(family)) << family;
  }
}

TEST(FontAllowlistTest, OtherFamiliesAreNot) {
  for (const char* family :
       {"Calibri", "Comic Sans MS", "Fira Code", "JetBrains Mono", "Roboto",
        "Verdana", "Segoe UI", "Menlo", "Aerie", "", "DejaVu", "Noto Sans Armenian",
        "Liberation Sans Narrow"}) {
    EXPECT_FALSE(IsFontFamilyAllowed(family)) << family;
  }
}

TEST(FontAllowlistTest, FamiliesMatchWithoutRegardToCase) {
  EXPECT_TRUE(IsFontFamilyAllowed("dejavu sans"));
  EXPECT_TRUE(IsFontFamilyAllowed("ARIAL"));
  EXPECT_TRUE(IsFontFamilyAllowed("Times new roman"));
  EXPECT_FALSE(IsFontFamilyAllowed("calibri"));
}

TEST(FontAllowlistTest, ALocalNameIsAnAllowedFamilyWithOrWithoutAStyle) {
  EXPECT_TRUE(IsLocalFontNameAllowed("DejaVu Sans"));
  EXPECT_TRUE(IsLocalFontNameAllowed("DejaVu Sans Bold"));
  EXPECT_TRUE(IsLocalFontNameAllowed("Liberation Mono Bold Italic"));
  EXPECT_TRUE(IsLocalFontNameAllowed("DejaVuSans"));
  EXPECT_TRUE(IsLocalFontNameAllowed("DejaVuSans-Bold"));
  EXPECT_TRUE(IsLocalFontNameAllowed("DejaVuSansMono-BoldOblique"));
  EXPECT_TRUE(IsLocalFontNameAllowed("LiberationSans-Regular"));
  EXPECT_TRUE(IsLocalFontNameAllowed("arial bold"));
}

TEST(FontAllowlistTest, ALocalNameOfAnotherFamilyIsNot) {
  EXPECT_FALSE(IsLocalFontNameAllowed(""));
  EXPECT_FALSE(IsLocalFontNameAllowed("Calibri"));
  EXPECT_FALSE(IsLocalFontNameAllowed("Calibri Bold"));
  EXPECT_FALSE(IsLocalFontNameAllowed("Roboto-Regular"));
  // The family is a prefix, but what follows is not a style.
  EXPECT_FALSE(IsLocalFontNameAllowed("Noto Sans Armenian"));
  EXPECT_FALSE(IsLocalFontNameAllowed("Liberation Sans Narrow"));
  EXPECT_FALSE(IsLocalFontNameAllowed("DejaVuSansCondensedExtra"));
  EXPECT_FALSE(IsLocalFontNameAllowed("DejaVu Sans Bold Secret"));
  EXPECT_FALSE(IsLocalFontNameAllowed("DejaVu Sans -"));
}

TEST(FontAllowlistTest, TheRestrictionNeedsTheFeaturesAndTheToken) {
  EXPECT_TRUE(ShouldRestrictFonts(
      {.master_enabled = true, .surface_enabled = true, .has_token = true}));
  EXPECT_FALSE(ShouldRestrictFonts(
      {.master_enabled = false, .surface_enabled = true, .has_token = true}));
  EXPECT_FALSE(ShouldRestrictFonts(
      {.master_enabled = true, .surface_enabled = false, .has_token = true}));
  // No token: the pref is off, or protection is off for the site or the
  // browser, so the real fonts come back.
  EXPECT_FALSE(ShouldRestrictFonts(
      {.master_enabled = true, .surface_enabled = true, .has_token = false}));
}

}  // namespace
}  // namespace aerie_fonts
