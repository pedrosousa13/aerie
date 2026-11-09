// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/ui/webui/aerie_guard/aerie_guard_panel_target.h"

#include <utility>

namespace aerie_guard {

AerieGuardPanelTarget::AerieGuardPanelTarget(
    content::WebContents* panel,
    aerie_adblock::GuardTarget target)
    : content::WebContentsUserData<AerieGuardPanelTarget>(*panel),
      target_(std::move(target)) {}

AerieGuardPanelTarget::~AerieGuardPanelTarget() = default;

// static
void AerieGuardPanelTarget::Set(content::WebContents* panel,
                                aerie_adblock::GuardTarget target) {
  if (AerieGuardPanelTarget* existing = FromWebContents(panel)) {
    existing->target_ = std::move(target);
    return;
  }
  CreateForWebContents(panel, std::move(target));
}

// static
aerie_adblock::GuardTarget AerieGuardPanelTarget::Get(
    content::WebContents* panel) {
  const AerieGuardPanelTarget* data = FromWebContents(panel);
  return data ? data->target_ : aerie_adblock::GuardTarget();
}

WEB_CONTENTS_USER_DATA_KEY_IMPL(AerieGuardPanelTarget);

}  // namespace aerie_guard
