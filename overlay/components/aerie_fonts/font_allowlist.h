// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_FONTS_FONT_ALLOWLIST_H_
#define COMPONENTS_AERIE_FONTS_FONT_ALLOWLIST_H_

#include <string_view>

// Which font names a page can match while "Restrict fonts" is on (issue #117),
// and when the restriction applies. Pure code: no Blink, no base, so it has unit
// tests. Blink only decides when to call it (patches/hardening/0020).
namespace aerie_fonts {

// Whether a page may match the font family `family`: the CSS generic keywords,
// the approved list in allowed_font_families.inc and the Microsoft aliases that
// fontconfig maps to Liberation. Compared without regard to ASCII case.
bool IsFontFamilyAllowed(std::string_view family);

// Whether `@font-face { src: local(name) }` may match `name`. A local name is a
// full name or a PostScript name (for example "DejaVu Sans Bold" or
// "DejaVuSans-Bold"), so it is allowed when it is an allowed family, or an
// allowed family followed only by style words (Bold, Italic, ...).
bool IsLocalFontNameAllowed(std::string_view name);

// What decides whether a context is restricted to the list.
struct Policy {
  // The HeliumNoise feature.
  bool master_enabled;
  // The HeliumNoiseFonts feature.
  bool surface_enabled;
  // Whether the browser sent this context a kFonts token. It sends none unless
  // the "Restrict fonts" pref is on and protection is on for the site, so
  // turning either off, or protection off for the site, gives the real fonts.
  bool has_token;
};

bool ShouldRestrictFonts(const Policy& policy);

}  // namespace aerie_fonts

#endif  // COMPONENTS_AERIE_FONTS_FONT_ALLOWLIST_H_
