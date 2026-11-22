// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_SIDEBAR_AERIE_SIDEBAR_HOVER_CARD_H_
#define COMPONENTS_AERIE_SIDEBAR_AERIE_SIDEBAR_HOVER_CARD_H_

#include <string>


// What the tab preview card of the sidebar decides without a view (issue #270,
// docs/sidebar-design.md, "Tab preview card, as built (#270)"): where it goes
// and what it says.
namespace aerie_sidebar {

// A rectangle and a size in whole pixels, in screen coordinates. The component
// has no views, so it has no gfx types; the view converts at its edge.
struct HoverCardBox {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;

  int right() const { return x + width; }
  int bottom() const { return y + height; }
  // Whether the two share any area (touching edges do not).
  bool Intersects(const HoverCardBox& other) const {
    return x < other.right() && other.x < right() && y < other.bottom() &&
           other.y < bottom();
  }
  bool Contains(const HoverCardBox& other) const {
    return other.x >= x && other.right() <= right() && other.y >= y &&
           other.bottom() <= bottom();
  }
  bool operator==(const HoverCardBox&) const = default;
};

// What a row tells the card controller.
enum class HoverCardTrigger {
  // The pointer entered or left the row.
  kPointerEntered,
  kPointerExited,
  // The row took keyboard focus (not by a mouse press), or lost focus.
  kFocused,
  kBlurred,
  // The row was pressed: the card goes, and stays away until the pointer comes
  // back.
  kPressed,
};

// Where the card goes, in screen coordinates. `row` is the row it describes,
// `screen` the area the card must stay in, `prefer_right` true when the
// sidebar is on the left (the card goes on the page side of the row). The card
// is level with the top of the row, moved up or down to stay in `screen`, and
// never covers `row`: when its preferred side has no room it goes on the other
// side; when neither has room it is kept on screen first.
// `card` uses only its width and height; the result is the card's own box.
HoverCardBox PlaceHoverCard(const HoverCardBox& row,
                            const HoverCardBox& card,
                            const HoverCardBox& screen,
                            bool prefer_right,
                            int gap);

// The host of a page, without a leading "www."; a page without a host (a
// file, an internal page) gives its scheme. Empty for an empty or invalid URL.
std::string HoverCardHost(const std::string& url_spec);

struct HoverCardText {
  // The title the sidebar shows; the host when that is empty.
  std::u16string title;
  // The host: the line under the title.
  std::u16string host;
  // The full URL, the second line; empty for a tab that has none yet.
  std::u16string url;
};

HoverCardText FormatHoverCardText(const std::u16string& title,
                                  const std::string& url_spec);

// What a screen reader gets as the row's description: the host, then the full
// URL ("example.com, https://example.com/a"). Empty when there is no URL. The
// title is already the row's name.
std::u16string HoverCardDescription(const HoverCardText& text);

}  // namespace aerie_sidebar

#endif  // COMPONENTS_AERIE_SIDEBAR_AERIE_SIDEBAR_HOVER_CARD_H_
