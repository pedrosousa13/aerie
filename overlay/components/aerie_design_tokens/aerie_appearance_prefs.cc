// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_design_tokens/aerie_appearance_prefs.h"

#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"

namespace aerie::appearance {

void RegisterProfilePrefs(PrefRegistrySimple* registry) {
  registry->RegisterStringPref(kThemePref, kDefaultTheme);
  registry->RegisterStringPref(kModePref, kModeSystem);
}

std::optional<tokens::ThemeId> ParseThemeId(std::string_view id) {
  for (int i = 0; i < tokens::kThemeCount; ++i) {
    if (id == tokens::kThemeIds[i]) {
      return static_cast<tokens::ThemeId>(i);
    }
  }
  return std::nullopt;
}

std::optional<tokens::ModeChoice> ParseModeChoice(std::string_view value) {
  if (value == kModeSystem) {
    return tokens::ModeChoice::kSystem;
  }
  if (value == kModeLight) {
    return tokens::ModeChoice::kLight;
  }
  if (value == kModeDark) {
    return tokens::ModeChoice::kDark;
  }
  return std::nullopt;
}

std::string_view ModeChoiceName(tokens::ModeChoice choice) {
  switch (choice) {
    case tokens::ModeChoice::kLight:
      return kModeLight;
    case tokens::ModeChoice::kDark:
      return kModeDark;
    case tokens::ModeChoice::kSystem:
      break;
  }
  return kModeSystem;
}

tokens::EffectiveLookInputs ReadGlobalChoice(const PrefService& prefs) {
  tokens::EffectiveLookInputs inputs;
  if (prefs.GetUserPrefValue(kThemePref)) {
    inputs.global_theme = ParseThemeId(prefs.GetString(kThemePref));
  }
  inputs.global_mode = ParseModeChoice(prefs.GetString(kModePref))
                           .value_or(tokens::ModeChoice::kSystem);
  return inputs;
}

}  // namespace aerie::appearance
