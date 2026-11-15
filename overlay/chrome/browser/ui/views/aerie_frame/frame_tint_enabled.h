// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_AERIE_FRAME_FRAME_TINT_ENABLED_H_
#define CHROME_BROWSER_UI_VIEWS_AERIE_FRAME_FRAME_TINT_ENABLED_H_

class BrowserView;

namespace aerie_frame {

// True when `browser_view`'s window frame is painted by the Aerie tint
// (aerie_frame::IsFrameTintActive for its profile and window type). Helium's
// frame fills, which would cover the tint, ask this and paint nothing.
bool ShouldTintFrame(const BrowserView* browser_view);

// The radius of a frame corner of this window: radius-xl while the tint is on,
// `helium_radius` (what Helium would use) otherwise.
int FrameRadius(const BrowserView* browser_view, int helium_radius);

// Whether the page is in a rounded card, given Helium's own pref.
bool RoundedFrameEnabled(const BrowserView* browser_view, bool helium_pref);

// Whether Helium's outline of the card is hidden: the tint is on. A theme that
// asks for a card border (OLED, High contrast) has the frame draw it just outside
// the card, so there is one border and it is the theme's.
bool CardBorderHidden(const BrowserView* browser_view);

}  // namespace aerie_frame

#endif  // CHROME_BROWSER_UI_VIEWS_AERIE_FRAME_FRAME_TINT_ENABLED_H_
