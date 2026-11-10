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

}  // namespace aerie_frame
