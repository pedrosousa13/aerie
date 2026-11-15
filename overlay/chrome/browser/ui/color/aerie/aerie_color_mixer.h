// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_COLOR_AERIE_AERIE_COLOR_MIXER_H_
#define CHROME_BROWSER_UI_COLOR_AERIE_AERIE_COLOR_MIXER_H_

#include <string>
#include <string_view>

#include "components/aerie_design_tokens/aerie_tokens.h"
#include "ui/color/color_provider_key.h"

namespace ui {
class ColorProvider;
}

// Chromium's own surfaces follow the effective Theme (issue #185, ADR 0007
// decision 7). AddAerieColorMixer() adds one mixer after the Chromium ones
// (patches/sidebar/0047), and it maps the Aerie tokens onto ui::ColorIds:
//
//   surface (`surface-popover`)   menu, bubble, dialog and omnibox popup
//                                 backgrounds, kColorPrimaryBackground and the
//                                 surface roles kColorSysSurface, ...Container
//                                 and ...ContainerLow
//   text (`text`)                 kColorPrimaryForeground, menu, dialog, label
//                                 and omnibox text, kColorSysOnSurface
//   text muted (`text-muted`)     kColorSecondaryForeground, the omnibox's dim
//                                 text, kColorSysOnSurfaceVariant, ...Subtle
//                                 and
//                                 ...Secondary
//   accent (Workspace hue)        kColorAccent, kColorSysPrimary
//   focus ring (the accent)       kColorFocusableBorderFocused,
//                                 kColorSysStateFocusRing
//   divider (`divider`)           kColorSeparator, kColorMenuSeparator,
//                                 kColorSysDivider
//   hairline (`border-hairline`)  kColorMenuBorder, kColorBubbleBorder
//
// The colour mode is the key's, which follows the Mode setting (the browser's
// colour scheme) or the window's own override. The key has no field for the
// Theme, so the Theme is process state: when it changes, SetActiveLook() says
// so and the caller resets the ColorProviderManager's cache and notifies the
// theme observers, and every window builds a provider again. Under forced
// colours the mixer does nothing: the system colours stand.
namespace aerie_color {

// The theme and the Workspace colour the mixer draws. Until the first
// SetActiveLook() the mixer adds nothing (Chromium's colours stand), so a
// test or a context without an Aerie window is not changed.
struct ActiveLook {
  aerie::tokens::ThemeId theme = aerie::tokens::ThemeId::kVivid;
  std::string colour;
};

// Returns true when the look differs from the one before.
bool SetActiveLook(aerie::tokens::ThemeId theme, std::string_view colour);
// Forgets the look: the mixer adds nothing again. For tests.
void ResetActiveLookForTesting();
bool HasActiveLook();
ActiveLook GetActiveLook();

void AddAerieColorMixer(ui::ColorProvider* provider,
                        const ui::ColorProviderKey& key);

}  // namespace aerie_color

#endif  // CHROME_BROWSER_UI_COLOR_AERIE_AERIE_COLOR_MIXER_H_
