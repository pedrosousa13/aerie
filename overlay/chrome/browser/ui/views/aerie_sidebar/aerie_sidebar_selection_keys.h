// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_SELECTION_KEYS_H_
#define CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_SELECTION_KEYS_H_

#include <optional>

#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"

// What a key or a click means for the selection of several rows (issue #265),
// shared by the row and the tile. The platform key is Ctrl, and Command on a Mac
// (`ui::EF_PLATFORM_ACCELERATOR`).
enum class AerieSidebarSelectionKey {
  // Shift+Up (Shift+Left on a tile): move the far end of the range back.
  kExtendBack,
  // Shift+Down (Shift+Right on a tile).
  kExtendForward,
  // Ctrl+A: every tab of the section of the focused row.
  kSelectAll,
  // Esc.
  kClear,
};

// A click that toggles one row instead of opening the tab.
inline bool IsSelectionToggleClick(const ui::MouseEvent& event) {
  return (event.flags() & ui::EF_PLATFORM_ACCELERATOR) != 0 &&
         !event.IsShiftDown();
}

// A click that selects the range from the anchor.
inline bool IsSelectionRangeClick(const ui::MouseEvent& event) {
  return event.IsShiftDown() && (event.flags() & ui::EF_PLATFORM_ACCELERATOR) == 0;
}

// The selection key that `event` is, if any. `grid`: a tile also extends with
// Shift+Left and Shift+Right.
inline std::optional<AerieSidebarSelectionKey> SelectionKeyFor(
    const ui::KeyEvent& event,
    bool grid) {
  if (event.IsAltDown()) {
    return std::nullopt;
  }
  const bool platform = (event.flags() & ui::EF_PLATFORM_ACCELERATOR) != 0;
  if (platform && !event.IsShiftDown() && event.key_code() == ui::VKEY_A) {
    return AerieSidebarSelectionKey::kSelectAll;
  }
  if (!platform && !event.IsShiftDown() && event.key_code() == ui::VKEY_ESCAPE) {
    return AerieSidebarSelectionKey::kClear;
  }
  if (platform || !event.IsShiftDown()) {
    return std::nullopt;
  }
  switch (event.key_code()) {
    case ui::VKEY_UP:
      return AerieSidebarSelectionKey::kExtendBack;
    case ui::VKEY_DOWN:
      return AerieSidebarSelectionKey::kExtendForward;
    case ui::VKEY_LEFT:
      return grid ? std::optional(AerieSidebarSelectionKey::kExtendBack)
                  : std::nullopt;
    case ui::VKEY_RIGHT:
      return grid ? std::optional(AerieSidebarSelectionKey::kExtendForward)
                  : std::nullopt;
    default:
      return std::nullopt;
  }
}

#endif  // CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_SELECTION_KEYS_H_
