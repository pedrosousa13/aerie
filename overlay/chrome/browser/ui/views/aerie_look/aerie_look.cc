// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/ui/views/aerie_look/aerie_look.h"

#include "components/aerie_design_tokens/aerie_appearance_prefs.h"
#include "components/aerie_frame/aerie_frame_palette.h"
#include "components/aerie_tab_model/tab_model_types.h"
#include "components/prefs/pref_service.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/native_theme/native_theme.h"

namespace aerie_look {

aerie::tokens::EffectiveLookInputs ReadLookInputs(
    const PrefService& prefs,
    const aerie_tab_model::Workspace* workspace,
    const ui::NativeTheme& native_theme) {
  aerie::tokens::EffectiveLookInputs inputs =
      aerie::appearance::ReadGlobalChoice(prefs);
  if (workspace) {
    inputs.workspace_theme = aerie::appearance::ParseThemeId(workspace->theme);
    inputs.workspace_mode = aerie::appearance::ParseModeChoice(workspace->mode);
  }
  inputs.os_dark = native_theme.preferred_color_scheme() ==
                   ui::NativeTheme::PreferredColorScheme::kDark;
  inputs.os_contrast_more = native_theme.preferred_contrast() ==
                            ui::NativeTheme::PreferredContrast::kMore;
  inputs.forced_colors =
      native_theme.forced_colors() != ui::ColorProviderKey::ForcedColors::kNone;
  return inputs;
}

aerie::tokens::EffectiveLook ComputeEffectiveLook(
    const PrefService& prefs,
    const aerie_tab_model::Workspace* workspace,
    const ui::NativeTheme& native_theme) {
  return aerie::tokens::ResolveEffectiveLook(
      ReadLookInputs(prefs, workspace, native_theme));
}

std::string WorkspaceColour(const aerie_tab_model::Workspace* workspace) {
  return workspace ? workspace->colour
                   : std::string(aerie_frame::kFallbackColour);
}

aerie_frame::SystemColours ReadSystemColours(
    const ui::ColorProvider& provider) {
  return {provider.GetColor(ui::kColorCssSystemWindow),
          provider.GetColor(ui::kColorCssSystemWindowText),
          provider.GetColor(ui::kColorCssSystemHighlight)};
}

}  // namespace aerie_look
