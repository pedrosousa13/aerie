// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_FRAME_AERIE_FRAME_PALETTE_H_
#define COMPONENTS_AERIE_FRAME_AERIE_FRAME_PALETTE_H_

#include <array>
#include <string_view>

#include "components/aerie_design_tokens/aerie_tokens.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/gfx/geometry/size.h"

class PrefService;

// What the window frame is painted from (issue #178, docs/sidebar-design.md,
// "Window frame and page card"). Headless: every value comes from the generated
// token tables (aerie_tokens.h) and the Workspace colour, so a test can check
// the frame without a window. There is no colour constant in this component.
namespace aerie_frame {

// The theme of this slice. Theme switching is slice I (#142): until then every
// window uses the default theme.
inline constexpr aerie::tokens::ThemeId kDefaultTheme =
    aerie::tokens::ThemeId::kVivid;

// The Workspace colour that stands in for an unknown name.
inline constexpr std::string_view kFallbackColour = "dusk";

// The frame, resolved for one theme, one colour mode and one Workspace colour.
struct FramePalette {
  SkColor frame_base;
  // The gradient stops of the Workspace colour, already mixed toward
  // `frame_base` at the theme's tint strength (mixing is linear, so a gradient
  // of mixed stops equals the mix of the gradient). `stop_count` are used.
  std::array<SkColor, 3> stops;
  int stop_count;
  int angle_degrees;
  // Opacity of black over the tint, and of white light at the corner nearest the
  // sidebar.
  float shade;
  float glow;
  // The colours the sidebar draws on the frame.
  SkColor text;
  SkColor text_muted;
  // The Workspace hue mixed toward the theme's accent base. Also the focus ring.
  SkColor accent;
  // The address pill's field colour (translucent, so it is drawn over the frame).
  SkColor field;
  // The page card.
  SkColor card_surface;
  int card_radius;
  aerie::tokens::Shadow card_shadow;
  int card_border_width;
  SkColor card_border_color;
};

// `colour` is a preset name from the tokens ("dusk") or a custom "#rrggbb".
// Anything else gives the fallback colour. A custom colour is one gradient stop
// and also the accent hue.
FramePalette ResolveFramePalette(aerie::tokens::ThemeId theme,
                                 aerie::tokens::ColorMode mode,
                                 std::string_view colour);

// What the frame paints at `point` of a window of `size` (the C++ twin of the
// frame model in check_contrast.py: gradient at the tint angle, then the shade,
// then the glow). `sidebar_on_right` mirrors it, so the glow sits at the top
// right corner.
SkColor FrameColorAt(const FramePalette& palette,
                     const gfx::Size& size,
                     bool sidebar_on_right,
                     const gfx::PointF& point);

// `color` laid over `under` (source-over), the result opaque.
SkColor Over(SkColor color, SkColor under);

// WCAG contrast ratio of two opaque colours.
double ContrastRatio(SkColor a, SkColor b);

}  // namespace aerie_frame

#endif  // COMPONENTS_AERIE_FRAME_AERIE_FRAME_PALETTE_H_
