// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_sidebar/aerie_sidebar_palette.h"

#include <cmath>

#include "components/aerie_frame/aerie_frame_palette.h"

namespace aerie_sidebar {

namespace {

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
  return palette;
}

}  // namespace aerie_sidebar
