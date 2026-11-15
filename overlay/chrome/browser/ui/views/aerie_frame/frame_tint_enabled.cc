// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/ui/views/aerie_frame/frame_tint_enabled.h"

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "components/aerie_frame/aerie_frame_enabled.h"
#include "components/prefs/pref_service.h"

namespace aerie_frame {

bool ShouldTintFrame(const BrowserView* browser_view) {
  return browser_view &&
         IsFrameTintActive(*browser_view->GetProfile()->GetPrefs(),
                           browser_view->GetIsNormalType());
}

int FrameRadius(const BrowserView* browser_view, int helium_radius) {
  return aerie_frame::FrameRadius(ShouldTintFrame(browser_view), helium_radius);
}

bool RoundedFrameEnabled(const BrowserView* browser_view, bool helium_pref) {
  return aerie_frame::RoundedFrameEnabled(ShouldTintFrame(browser_view),
                                          helium_pref);
}

bool CardBorderHidden(const BrowserView* browser_view) {
  // The card's hairline (OLED, High contrast, forced colours) is drawn by the
  // frame, just outside the card (aerie_frame::PaintFrame), so Helium's own
  // outline never shows while the tint is on (issue #185).
  return ShouldTintFrame(browser_view);
}

}  // namespace aerie_frame
