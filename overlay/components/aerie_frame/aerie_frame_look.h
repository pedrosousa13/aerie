// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_FRAME_AERIE_FRAME_LOOK_H_
#define COMPONENTS_AERIE_FRAME_AERIE_FRAME_LOOK_H_

#include <string>
#include <string_view>

#include "components/aerie_design_tokens/aerie_tokens.h"
#include "components/aerie_frame/aerie_frame_palette.h"
#include "third_party/skia/include/core/SkColor.h"

// What a Theme gives the parts of the browser that Aerie does not draw itself,
// and what forced colours give the parts it does (issue #185). Headless, like
// the palette: every value is a token or an operating-system colour.
namespace aerie_frame {

// The colours Chromium's own surfaces (menus, dialogs, bubbles, the omnibox
// popup) get from the Aerie ColorMixer. They are the theme's popover surface
// and text, so those surfaces match the sidebar and the frame in every theme
// and mode.
struct ChromeColors {
  SkColor surface;     // `surface-popover`
  SkColor text;        // `text`
  SkColor text_muted;  // `text-muted`
  SkColor accent;      // The Workspace hue toward the theme's accent base.
  SkColor focus_ring;  // The accent.
  SkColor divider;     // `divider`
  SkColor hairline;    // `border-hairline`
};

ChromeColors ResolveChromeColors(aerie::tokens::ThemeId theme,
                                 aerie::tokens::ColorMode mode,
                                 std::string_view colour);

// The operating system's colours under forced colours (CSS `Canvas`,
// `CanvasText` and `Highlight`). The views read them from the ColorProvider,
// which the native theme fills in; the Aerie tint is not part of them.
struct SystemColours {
  SkColor background;
  SkColor text;
  SkColor accent;

  bool operator==(const SystemColours&) const = default;
};

// A frame of one flat colour, with the card's hairline in the text colour. No
// tint, shade, glow or strip.
FramePalette ResolveSystemFramePalette(const SystemColours& system);

// The preview swatch of a theme in Settings, as a CSS `background` value: the
// theme's frame with the Dusk Workspace colour, in light mode (dark mode for
// OLED, which has no light look). Every colour is a token. The Settings page
// only prints it.
std::string ThemeSwatchCss(aerie::tokens::ThemeId theme);

}  // namespace aerie_frame

#endif  // COMPONENTS_AERIE_FRAME_AERIE_FRAME_LOOK_H_
