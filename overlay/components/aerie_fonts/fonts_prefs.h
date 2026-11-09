// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_FONTS_FONTS_PREFS_H_
#define COMPONENTS_AERIE_FONTS_FONTS_PREFS_H_

class PrefRegistrySimple;

namespace aerie_fonts {

// Whether pages can match only the allowlist of common fonts (issue #117). A
// profile pref, off by default. The hardening page of Settings writes it
// through settingsPrivate; the name is also a string literal in the
// settings_private allowlist (patches/hardening/0020), and a test compares the
// two.
inline constexpr char kRestrictFonts[] = "aerie.fonts.restrict";

// Called from chrome::RegisterProfilePrefs (patches/hardening/0020).
void RegisterProfilePrefs(PrefRegistrySimple* registry);

}  // namespace aerie_fonts

#endif  // COMPONENTS_AERIE_FONTS_FONTS_PREFS_H_
