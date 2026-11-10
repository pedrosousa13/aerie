// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/ui/views/aerie_frame/frame_tint_enabled.h"

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "components/aerie_frame/aerie_frame_enabled.h"
#include "components/aerie_frame/aerie_frame_palette.h"
#include "ui/color/color_provider_key.h"
#include "ui/views/widget/widget.h"
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
  if (!ShouldTintFrame(browser_view)) {
    return false;
  }
  const views::Widget* widget = browser_view->GetWidget();
  const bool dark = widget && widget->GetColorMode() ==
                                  ui::ColorProviderKey::ColorMode::kDark;
  return CardBorderWidth(kDefaultTheme,
                         dark ? aerie::tokens::ColorMode::kDark
                              : aerie::tokens::ColorMode::kLight) == 0;
}

}  // namespace aerie_frame
