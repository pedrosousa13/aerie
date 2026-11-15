// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_DESIGN_TOKENS_AERIE_EFFECTIVE_LOOK_H_
#define COMPONENTS_AERIE_DESIGN_TOKENS_AERIE_EFFECTIVE_LOOK_H_

#include <optional>

#include "components/aerie_design_tokens/aerie_tokens.h"

// Which Theme and colour mode the chrome is drawn in (issue #185,
// docs/sidebar-design.md "Themes" and "prefers-contrast"). A pure function of
// the global choice, a Workspace override and what the operating system asks
// for, so the whole table is unit-tested without a window.
namespace aerie::tokens {

// The three modes a person can choose. System follows the operating system.
enum class ModeChoice { kSystem, kLight, kDark };

struct EffectiveLookInputs {
  // The global default theme, or empty when the user never chose one. Empty is
  // not Vivid: it is the only case in which `prefers-contrast: more` selects
  // High contrast. Choosing Vivid on purpose is a choice.
  std::optional<ThemeId> global_theme;
  ModeChoice global_mode = ModeChoice::kSystem;
  // The Workspace's own theme and mode; empty is "default" (use the global one).
  // Slice M (M5) adds the editor; M2 has one Workspace and no override.
  std::optional<ThemeId> workspace_theme;
  std::optional<ModeChoice> workspace_mode;
  // What the operating system says.
  bool os_dark = false;
  bool os_contrast_more = false;
  bool forced_colors = false;
};

struct EffectiveLook {
  // A theme that has a table in `mode`: OLED in light mode is Clean.
  ThemeId theme = ThemeId::kVivid;
  ColorMode mode = ColorMode::kLight;
  // Forced colours: the chrome is drawn in the operating system's colours and
  // the theme only decides what is not a colour.
  bool system_colours = false;

  bool operator==(const EffectiveLook&) const = default;
};

EffectiveLook ResolveEffectiveLook(const EffectiveLookInputs& inputs);

}  // namespace aerie::tokens

#endif  // COMPONENTS_AERIE_DESIGN_TOKENS_AERIE_EFFECTIVE_LOOK_H_
