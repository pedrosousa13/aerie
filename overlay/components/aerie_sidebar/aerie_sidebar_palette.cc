// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_sidebar/aerie_sidebar_palette.h"

#include <cmath>

#include "components/aerie_frame/aerie_frame_palette.h"

namespace aerie_sidebar {

namespace {

// WCAG 1.4.11: a graphic that carries meaning needs 3:1 against its ground.
constexpr double kGraphicContrast = 3.0;

SkColor WithShare(SkColor color, float share) {
  return SkColorSetA(color, static_cast<uint8_t>(std::lround(share * 255.0f)));
}

}  // namespace

SidebarPalette ResolveSidebarPalette(aerie::tokens::ThemeId theme,
                                     aerie::tokens::ColorMode mode,
                                     std::string_view colour) {
  const aerie_frame::FramePalette frame =
      aerie_frame::ResolveFramePalette(theme, mode, colour);
  const aerie::tokens::ThemeTokens& tokens =
      aerie::tokens::GetThemeTokens(theme, mode);
  SidebarPalette palette;
  palette.text = frame.text;
  palette.text_muted = frame.text_muted;
  palette.accent = frame.accent;
  palette.row_hover = tokens.color_row_hover;
  palette.row_selected = tokens.color_row_selected;
  palette.divider = tokens.color_divider;
  palette.control_rest = WithShare(frame.text, kControlRestShare);
  palette.control_hover = WithShare(frame.text, kControlHoverShare);
  palette.control_close_hover = tokens.color_status_danger;
  palette.peek_surface =
      frame.stop_count > 0 ? frame.stops[0] : frame.frame_base;
  palette.peek_shadow = tokens.shadow_peek;
  palette.popover_surface = tokens.color_surface_popover;
  palette.popover_text = tokens.color_text;
  palette.popover_text_muted = tokens.color_text_muted;
  palette.popover_accent =
      aerie_frame::ContrastRatio(frame.accent, tokens.color_surface_popover) >=
              kGraphicContrast
          ? frame.accent
          : tokens.color_text;
  palette.focus_ring_width = tokens.size_focus_ring_width;
  return palette;
}

SidebarPalette ResolveSystemSidebarPalette(
    const aerie_frame::SystemColours& system) {
  // The fills are opaque mixes of the two system colours: a hover is a faint
  // text-coloured wash, the selected row a strong one.
  auto mix = [&](float share) {
    return aerie_frame::Over(WithShare(system.text, share), system.background);
  };
  SidebarPalette palette;
  palette.text = system.text;
  palette.text_muted = system.text;
  palette.accent = system.accent;
  palette.row_hover = mix(0.15f);
  palette.row_selected = mix(0.30f);
  palette.divider = system.text;
  palette.control_rest = system.text;
  palette.control_hover = system.accent;
  palette.control_close_hover = system.accent;
  palette.peek_surface = system.background;
  palette.peek_shadow = aerie::tokens::Shadow{};
  palette.popover_surface = system.background;
  palette.popover_text = system.text;
  palette.popover_text_muted = system.text;
  palette.popover_accent = system.accent;
  palette.focus_ring_width = aerie::tokens::kSizeFocusRingWidth + 1;
  return palette;
}

}  // namespace aerie_sidebar
