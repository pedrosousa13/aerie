// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_DESIGN_TOKENS_AERIE_APPEARANCE_PREFS_H_
#define COMPONENTS_AERIE_DESIGN_TOKENS_AERIE_APPEARANCE_PREFS_H_

#include <optional>
#include <string_view>

#include "components/aerie_design_tokens/aerie_effective_look.h"
#include "components/aerie_design_tokens/aerie_tokens.h"

class PrefRegistrySimple;
class PrefService;

// The global default look (issue #185): a Theme and a Mode. Registered by
// chrome::RegisterProfilePrefs (patches/sidebar/0046). The names are also
// string literals in the settings_private allowlist, and a test compares them.
namespace aerie::appearance {

// One of the theme ids of kThemeIds. Default "vivid". The default is not a
// choice: see ReadGlobalChoice().
inline constexpr char kThemePref[] = "aerie.appearance.theme";
inline constexpr char kDefaultTheme[] = "vivid";

// "system" (the default), "light" or "dark".
inline constexpr char kModePref[] = "aerie.appearance.mode";
inline constexpr char kModeSystem[] = "system";
inline constexpr char kModeLight[] = "light";
inline constexpr char kModeDark[] = "dark";

void RegisterProfilePrefs(PrefRegistrySimple* registry);

// A persisted theme id, or empty for anything else, "default" among it.
std::optional<tokens::ThemeId> ParseThemeId(std::string_view id);
// "system", "light" or "dark", or empty for anything else.
std::optional<tokens::ModeChoice> ParseModeChoice(std::string_view value);
std::string_view ModeChoiceName(tokens::ModeChoice choice);

// The global half of the inputs: the theme only when the user set the pref (the
// default is no choice), and the mode, system when the stored value is bad.
// The Workspace override and the operating system are the caller's.
tokens::EffectiveLookInputs ReadGlobalChoice(const PrefService& prefs);

}  // namespace aerie::appearance

#endif  // COMPONENTS_AERIE_DESIGN_TOKENS_AERIE_APPEARANCE_PREFS_H_
