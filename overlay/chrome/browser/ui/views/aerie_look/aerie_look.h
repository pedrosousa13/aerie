// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_AERIE_LOOK_AERIE_LOOK_H_
#define CHROME_BROWSER_UI_VIEWS_AERIE_LOOK_AERIE_LOOK_H_

#include <string>

#include "components/aerie_design_tokens/aerie_effective_look.h"
#include "components/aerie_frame/aerie_frame_look.h"

class PrefService;

namespace aerie_tab_model {
struct Workspace;
}

namespace ui {
class ColorProvider;
class NativeTheme;
}  // namespace ui

// Where the effective look (issue #185) gets its inputs. The rule is the pure
// function aerie::tokens::ResolveEffectiveLook(); this file reads the inputs:
// the global prefs, the Workspace record (slice N) and the native theme, whose
// dark, contrast and forced-colours state the operating system fills in.
namespace aerie_look {

// `workspace` may be null (the Tab Model has not loaded): then there is no
// override. A Workspace `theme` or `mode` of "default" is no override.
aerie::tokens::EffectiveLookInputs ReadLookInputs(
    const PrefService& prefs,
    const aerie_tab_model::Workspace* workspace,
    const ui::NativeTheme& native_theme);

aerie::tokens::EffectiveLook ComputeEffectiveLook(
    const PrefService& prefs,
    const aerie_tab_model::Workspace* workspace,
    const ui::NativeTheme& native_theme);

// The Workspace colour, or the fallback colour before the model loads.
std::string WorkspaceColour(const aerie_tab_model::Workspace* workspace);

// The operating system's colours as the ColorProvider holds them under forced
// colours: the CSS system colours `Window`, `WindowText` and `Highlight`.
aerie_frame::SystemColours ReadSystemColours(const ui::ColorProvider& provider);

}  // namespace aerie_look

#endif  // CHROME_BROWSER_UI_VIEWS_AERIE_LOOK_AERIE_LOOK_H_
