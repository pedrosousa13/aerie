// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_frame/aerie_frame_palette.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>

namespace aerie_frame {
namespace {

using aerie::tokens::ColorMode;

struct Rgb {
  double r;
  double g;
  double b;
};

Rgb ToRgb(SkColor c) {
  return {double(SkColorGetR(c)), double(SkColorGetG(c)), double(SkColorGetB(c))};
}

// `share` of `a`, the rest of `b`.
Rgb Mix(Rgb a, Rgb b, double share) {
  return {a.r * share + b.r * (1 - share), a.g * share + b.g * (1 - share),
          a.b * share + b.b * (1 - share)};
}

uint8_t Round(double v) {
  return static_cast<uint8_t>(std::lround(std::clamp(v, 0.0, 255.0)));
}

SkColor ToColor(Rgb c) {
  return SkColorSetRGB(Round(c.r), Round(c.g), Round(c.b));
}

std::optional<int> HexDigit(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  return std::nullopt;
}

// "#rrggbb".
std::optional<SkColor> ParseCustomColour(std::string_view text) {
  if (text.size() != 7 || text[0] != '#') {
    return std::nullopt;
  }
  int v[6];
  for (int i = 0; i < 6; ++i) {
    std::optional<int> d = HexDigit(text[i + 1]);
    if (!d) {
      return std::nullopt;
    }
    v[i] = *d;
  }
  return SkColorSetRGB(v[0] * 16 + v[1], v[2] * 16 + v[3], v[4] * 16 + v[5]);
}

int PresetIndex(std::string_view colour) {
  for (int i = 0; i < aerie::tokens::kWorkspacePresetCount; ++i) {
    if (colour == aerie::tokens::kWorkspacePresetIds[i]) {
      return i;
    }
  }
  return -1;
}

double Linear(double channel) {
  const double v = channel / 255.0;
  return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
}

double Luminance(SkColor c) {
  return 0.2126 * Linear(SkColorGetR(c)) + 0.7152 * Linear(SkColorGetG(c)) +
         0.0722 * Linear(SkColorGetB(c));
}

}  // namespace

FramePalette ResolveFramePalette(aerie::tokens::ThemeId theme,
                                 ColorMode mode,
                                 std::string_view colour) {
  const aerie::tokens::ThemeTokens& t =
      aerie::tokens::GetThemeTokens(theme, mode);

  std::array<SkColor, 3> raw_stops{};
  int stop_count = 0;
  SkColor hue = 0;
  if (std::optional<SkColor> custom = ParseCustomColour(colour)) {
    raw_stops[0] = *custom;
    stop_count = 1;
    hue = *custom;
  } else {
    int index = PresetIndex(colour);
    if (index < 0) {
      index = PresetIndex(kFallbackColour);
    }
    const aerie::tokens::WorkspacePreset& preset =
        aerie::tokens::kWorkspacePresets[index];
    stop_count = preset.stop_count;
    for (int i = 0; i < stop_count; ++i) {
      raw_stops[i] = preset.stops[i];
    }
    hue = mode == ColorMode::kLight ? preset.accent_light : preset.accent_dark;
  }

  FramePalette p{};
  p.frame_base = t.color_frame_base;
  p.stop_count = stop_count;
  for (int i = 0; i < stop_count; ++i) {
    p.stops[i] = ToColor(
        Mix(ToRgb(raw_stops[i]), ToRgb(t.color_frame_base), t.theme_tint_strength));
  }
  p.shade_colour = t.color_frame_shade;
  p.glow_colour = t.color_frame_glow;
  p.angle_degrees = aerie::tokens::kTintAngle;
  p.shade = t.theme_shade;
  p.glow = t.theme_glow;
  p.text = t.theme_text;
  p.text_muted = t.theme_text_muted;
  p.accent = ToColor(
      Mix(ToRgb(hue), ToRgb(t.theme_accent_base), t.theme_accent_hue_share));
  p.field = t.color_field;
  p.card_surface = t.color_content_surface;
  p.card_radius = aerie::tokens::kRadiusXl;
  p.card_shadow = t.shadow_card;
  p.card_border_width = t.theme_card_border_width;
  p.card_border_color = t.theme_card_border_color;
  return p;
}

SkColor FrameColorAt(const FramePalette& palette,
                     const gfx::Size& size,
                     bool sidebar_on_right,
                     const gfx::PointF& point) {
  const double w = size.width();
  const double h = size.height();
  const double x = sidebar_on_right ? w - point.x() : point.x();
  const double y = point.y();

  // The gradient: CSS linear-gradient(angle) over the box.
  const double angle = palette.angle_degrees * std::numbers::pi / 180.0;
  const double length = std::abs(w * std::sin(angle)) + std::abs(h * std::cos(angle));
  double t = ((x - w / 2) * std::sin(angle) - (y - h / 2) * std::cos(angle)) / length + 0.5;
  t = std::clamp(t, 0.0, 1.0);
  Rgb c;
  if (palette.stop_count == 1) {
    c = ToRgb(palette.stops[0]);
  } else {
    const double s = t * (palette.stop_count - 1);
    const int i = std::min(static_cast<int>(s), palette.stop_count - 2);
    c = Mix(ToRgb(palette.stops[i + 1]), ToRgb(palette.stops[i]), s - i);
  }

  // The shade layer over the tint, in the shade token's colour.
  c = Mix(ToRgb(palette.shade_colour), c, palette.shade);

  // The glow, in the glow token's colour: a radial gradient of 120% by 80% of
  // the box at the top corner nearest the sidebar (top left, or top right with
  // the sidebar on the right: `x` is already mirrored), fading out at 60% of
  // that.
  const double d = std::hypot(x / (1.2 * w), y / (0.8 * h));
  const double glow = palette.glow * std::max(0.0, 1.0 - d / 0.6);
  c = Mix(ToRgb(palette.glow_colour), c, glow);
  return ToColor(c);
}

int CardBorderWidth(aerie::tokens::ThemeId theme, ColorMode mode) {
  return aerie::tokens::GetThemeTokens(theme, mode).theme_card_border_width;
}

SkColor Over(SkColor color, SkColor under) {
  return ToColor(Mix(ToRgb(color), ToRgb(under), SkColorGetA(color) / 255.0));
}

double ContrastRatio(SkColor a, SkColor b) {
  const double la = std::max(Luminance(a), Luminance(b));
  const double lb = std::min(Luminance(a), Luminance(b));
  return (la + 0.05) / (lb + 0.05);
}

}  // namespace aerie_frame
