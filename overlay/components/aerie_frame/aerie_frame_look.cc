// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_frame/aerie_frame_look.h"

#include <algorithm>
#include <cstdio>

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
  // The token is white in every theme. On a light accent (High contrast dark,
  // the pale accents of dark themes) white is unreadable, so the better of
  // black and white takes over below the floor: 4.5:1, and 7:1 for High
  // contrast.
  const double floor =
      theme == aerie::tokens::ThemeId::kHighContrast ? 7.0 : 4.5;
  c.on_accent = t.color_text_on_accent;
  if (ContrastRatio(c.on_accent, c.accent) < floor) {
    c.on_accent = ContrastRatio(SK_ColorBLACK, c.accent) >=
                          ContrastRatio(SK_ColorWHITE, c.accent)
                      ? SK_ColorBLACK
                      : SK_ColorWHITE;
  }
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

std::string ThemeSwatchCss(aerie::tokens::ThemeId theme) {
  const aerie::tokens::ColorMode mode =
      aerie::tokens::ThemeSupportsMode(theme, aerie::tokens::ColorMode::kLight)
          ? aerie::tokens::ColorMode::kLight
          : aerie::tokens::ColorMode::kDark;
  const FramePalette p = ResolveFramePalette(theme, mode, "dusk");
  std::string css = "linear-gradient(135deg";
  for (int i = 0; i < std::max(p.stop_count, 1); ++i) {
    // A theme with no tint (OLED, High contrast, Mono) has the base colour.
    const SkColor c = p.stop_count > 0 ? p.stops[i] : p.frame_base;
    char hex[16];
    std::snprintf(hex, sizeof(hex), ", #%02X%02X%02X", SkColorGetR(c),
                  SkColorGetG(c), SkColorGetB(c));
    css += hex;
  }
  css += ")";
  return css;
}

}  // namespace aerie_frame
