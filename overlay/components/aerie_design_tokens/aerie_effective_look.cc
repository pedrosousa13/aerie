// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_design_tokens/aerie_effective_look.h"

namespace aerie::tokens {

EffectiveLook ResolveEffectiveLook(const EffectiveLookInputs& in) {
  EffectiveLook look;

  // Theme: the Workspace override, else the global choice, else the default,
  // which is High contrast under `prefers-contrast: more`. Both choices beat
  // the contrast switch.
  if (in.workspace_theme) {
    look.theme = *in.workspace_theme;
  } else if (in.global_theme) {
    look.theme = *in.global_theme;
  } else {
    look.theme = in.os_contrast_more ? ThemeId::kHighContrast : ThemeId::kVivid;
  }

  // Mode: the Workspace override, else the global choice. System follows the
  // operating system.
  const ModeChoice choice = in.workspace_mode.value_or(in.global_mode);
  switch (choice) {
    case ModeChoice::kLight:
      look.mode = ColorMode::kLight;
      break;
    case ModeChoice::kDark:
      look.mode = ColorMode::kDark;
      break;
    case ModeChoice::kSystem:
      look.mode = in.os_dark ? ColorMode::kDark : ColorMode::kLight;
      break;
  }

  // A theme without a table in this mode (OLED in light mode) shows Clean.
  if (!ThemeSupportsMode(look.theme, look.mode)) {
    look.theme = ThemeId::kClean;
  }
  look.system_colours = in.forced_colors;
  return look;
}

}  // namespace aerie::tokens
