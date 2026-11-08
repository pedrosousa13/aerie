// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/aerie_adblock/aerie_protection_tab_state.h"

#include "content/public/browser/web_contents.h"

AerieProtectionTabState::AerieProtectionTabState(
    content::WebContents* web_contents)
    : content::WebContentsUserData<AerieProtectionTabState>(*web_contents) {}

AerieProtectionTabState::~AerieProtectionTabState() = default;

// static
AerieProtectionTabState* AerieProtectionTabState::GetOrCreate(
    content::WebContents* web_contents) {
  CreateForWebContents(web_contents);
  return FromWebContents(web_contents);
}

WEB_CONTENTS_USER_DATA_KEY_IMPL(AerieProtectionTabState);
