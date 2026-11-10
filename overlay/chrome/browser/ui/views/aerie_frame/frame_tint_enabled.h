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

}  // namespace aerie_frame

#endif  // CHROME_BROWSER_UI_VIEWS_AERIE_FRAME_FRAME_TINT_ENABLED_H_
