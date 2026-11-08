// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_AERIE_ADBLOCK_AERIE_PROTECTION_TAB_STATE_H_
#define CHROME_BROWSER_AERIE_ADBLOCK_AERIE_PROTECTION_TAB_STATE_H_

#include "components/aerie_adblock/browser/protection_policy.h"
#include "content/public/browser/web_contents_user_data.h"

namespace content {
class WebContents;
}  // namespace content

// The hosts the user chose "Continue anyway" for in one tab. In memory only:
// it goes away with the tab, so a choice never outlives the session and never
// reaches another tab, another profile or an incognito window.
class AerieProtectionTabState
    : public content::WebContentsUserData<AerieProtectionTabState> {
 public:
  ~AerieProtectionTabState() override;

  AerieProtectionTabState(const AerieProtectionTabState&) = delete;
  AerieProtectionTabState& operator=(const AerieProtectionTabState&) = delete;

  // The state of |web_contents|, created on first use.
  static AerieProtectionTabState* GetOrCreate(
      content::WebContents* web_contents);

  aerie_adblock::SessionAllowlist& allowlist() { return allowlist_; }

 private:
  explicit AerieProtectionTabState(content::WebContents* web_contents);
  friend class content::WebContentsUserData<AerieProtectionTabState>;
  WEB_CONTENTS_USER_DATA_KEY_DECL();

  aerie_adblock::SessionAllowlist allowlist_;
};

#endif  // CHROME_BROWSER_AERIE_ADBLOCK_AERIE_PROTECTION_TAB_STATE_H_
