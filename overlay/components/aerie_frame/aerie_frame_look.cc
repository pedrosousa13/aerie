// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_frame/aerie_frame_look.h"

namespace aerie_frame {

namespace {
// The hairline of the card under forced colours.
constexpr int kSystemCardBorderWidth = 1;
}  // namespace

ChromeColors ResolveChromeColors(aerie::tokens::ThemeId theme,
                                 aerie::tokens::ColorMode mode,
                                 std::string_view colour) {
  const aerie::tokens::ThemeTokens& t =
      aerie::tokens::GetThemeTokens(theme, mode);
  const FramePalette frame = ResolveFramePalette(theme, mode, colour);
  ChromeColors c;
  c.surface = t.color_surface_popover;
  c.text = t.color_text;
  c.text_muted = t.color_text_muted;
  c.accent = frame.accent;
  c.focus_ring = frame.accent;
  c.divider = t.color_divider;
  c.hairline = t.color_border_hairline;
  return c;
}

FramePalette ResolveSystemFramePalette(const SystemColours& system) {
  FramePalette p{};
  p.frame_base = system.background;
  p.stop_count = 1;
  p.stops[0] = system.background;
  p.angle_degrees = aerie::tokens::kTintAngle;
  p.shade = 0;
  p.glow = 0;
  p.shade_colour = system.background;
  p.glow_colour = system.background;
  p.text = system.text;
  p.text_muted = system.text;
  p.accent = system.accent;
  p.field = system.background;
  p.strip_height = 0;
  p.strip_stop_count = 0;
  p.card_surface = system.background;
  p.card_radius = aerie::tokens::kRadiusXl;
  p.card_shadow = aerie::tokens::Shadow{};
  p.card_border_width = kSystemCardBorderWidth;
  p.card_border_color = system.text;
  return p;
}

}  // namespace aerie_frame
