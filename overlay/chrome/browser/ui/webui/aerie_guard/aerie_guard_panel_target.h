// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_AERIE_GUARD_AERIE_GUARD_PANEL_TARGET_H_
#define CHROME_BROWSER_UI_WEBUI_AERIE_GUARD_AERIE_GUARD_PANEL_TARGET_H_

#include "components/aerie_adblock/browser/guard_target.h"
#include "content/public/browser/web_contents_user_data.h"

namespace aerie_guard {

// The tab and site one Guard panel acts on, kept on the panel's own
// WebContents. The Guard icon sets it, in the browser process, when it opens
// the panel; the panel's page handler reads it when the page creates the
// handler. It lives on the WebContents, not in the WebUI controller, because a
// navigation can replace the controller.
//
// A panel with no target (chrome://aerie-guard.top-chrome loaded in an
// ordinary tab) gets an empty one.
class AerieGuardPanelTarget
    : public content::WebContentsUserData<AerieGuardPanelTarget> {
 public:
  ~AerieGuardPanelTarget() override;

  // Browser code only: nothing the page sends reaches this.
  static void Set(content::WebContents* panel,
                  aerie_adblock::GuardTarget target);
  // The target of |panel|, or an empty one.
  static aerie_adblock::GuardTarget Get(content::WebContents* panel);

 private:
  friend class content::WebContentsUserData<AerieGuardPanelTarget>;
  AerieGuardPanelTarget(content::WebContents* panel,
                        aerie_adblock::GuardTarget target);

  aerie_adblock::GuardTarget target_;

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

}  // namespace aerie_guard

#endif  // CHROME_BROWSER_UI_WEBUI_AERIE_GUARD_AERIE_GUARD_PANEL_TARGET_H_
