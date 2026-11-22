// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_sidebar/aerie_sidebar_hover_card.h"

#include <algorithm>

#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "url/gurl.h"

namespace aerie_sidebar {

HoverCardBox PlaceHoverCard(const HoverCardBox& row,
                            const HoverCardBox& card,
                            const HoverCardBox& screen,
                            bool prefer_right,
                            int gap) {
  auto at_right = [&] { return row.right() + gap; };
  auto at_left = [&] { return row.x - gap - card.width; };
  auto fits = [&](int x) {
    return x >= screen.x && x + card.width <= screen.right();
  };
  int x = prefer_right ? at_right() : at_left();
  if (!fits(x)) {
    const int other = prefer_right ? at_left() : at_right();
    if (fits(other)) {
      x = other;
    } else {
      // No room on either side: stay on screen, as near the preferred side as
      // the screen allows.
      x = std::clamp(x, screen.x,
                     std::max(screen.x, screen.right() - card.width));
    }
  }
  const int max_y = std::max(screen.y, screen.bottom() - card.height);
  const int y = std::clamp(row.y, screen.y, max_y);
  return HoverCardBox{x, y, card.width, card.height};
}

bool HoverCardPrefersRight(const HoverCardBox& row,
                           const HoverCardBox& window) {
  return row.x + row.width / 2 <= window.x + window.width / 2;
}

std::string HoverCardHost(const std::string& url_spec) {
  const GURL url(url_spec);
  if (!url.is_valid()) {
    return std::string();
  }
  std::string host(url.host());
  if (base::StartsWith(host, "www.")) {
    host.erase(0, 4);
  }
  if (host.empty()) {
    host = std::string(url.scheme());
  }
  return host;
}

HoverCardText FormatHoverCardText(const std::u16string& title,
                                  const std::string& url_spec) {
  HoverCardText text;
  text.host = base::UTF8ToUTF16(HoverCardHost(url_spec));
  text.title = title.empty() ? text.host : title;
  text.url = base::UTF8ToUTF16(url_spec);
  return text;
}

std::u16string HoverCardDescription(const HoverCardText& text) {
  if (text.url.empty()) {
    return std::u16string();
  }
  return text.host + u", " + text.url;
}

}  // namespace aerie_sidebar
