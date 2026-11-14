// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_SIDEBAR_AERIE_SIDEBAR_PALETTE_H_
#define COMPONENTS_AERIE_SIDEBAR_AERIE_SIDEBAR_PALETTE_H_

#include <string_view>

#include "components/aerie_design_tokens/aerie_tokens.h"
#include "third_party/skia/include/core/SkColor.h"

// The colours the sidebar's own views draw with (issue #215, item 7). Headless:
// every value comes from the generated token tables (aerie_tokens.h) for the
// theme, the colour mode and the Workspace colour, through the frame palette
// that the sidebar sits on, so the rows and the frame cannot disagree. There is
// no colour constant in this component or in the views, and no Chromium system
// colour role: a test scans for both.
namespace aerie_sidebar {

struct SidebarPalette {
  // Text and icons on the frame (the frame's own text colours, `text` and
  // `text-muted`; a theme such as OLED overrides them).
  SkColor text;
  SkColor text_muted;
  // The Workspace hue mixed toward the theme's accent base: the focus ring.
  SkColor accent;
  // `row-hover` and `row-selected`: translucent, drawn over the frame.
  SkColor row_hover;
  SkColor row_selected;
  // `divider`.
  SkColor divider;
  // The window controls: a circle of the text colour at a share of opacity,
  // and the danger colour under the pointer on the close control.
  SkColor control_rest;
  SkColor control_hover;
  SkColor control_close_hover;
  // The opaque surface of a floating (peeked) sidebar, and its shadow
  // (`shadow-peek`, issue #184): the frame colour of the Workspace, because the
  // sidebar is then over the page and not over the frame.
  SkColor peek_surface;
  aerie::tokens::Shadow peek_shadow;
};

// The share of the text colour that a window control circle shows at rest and
// under the pointer. These are the mockup's `color-mix` percentages
// (prototypes/sidebar/sidebar.css, `.wc-btn`), not colours.
inline constexpr float kControlRestShare = 0.22f;
inline constexpr float kControlHoverShare = 0.40f;

// `colour` is a preset name from the tokens ("dusk") or a custom "#rrggbb", as
// for aerie_frame::ResolveFramePalette; anything else gives the fallback
// colour.
SidebarPalette ResolveSidebarPalette(aerie::tokens::ThemeId theme,
                                     aerie::tokens::ColorMode mode,
                                     std::string_view colour);

}  // namespace aerie_sidebar

#endif  // COMPONENTS_AERIE_SIDEBAR_AERIE_SIDEBAR_PALETTE_H_
