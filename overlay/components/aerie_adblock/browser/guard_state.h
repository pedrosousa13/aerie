// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_ADBLOCK_BROWSER_GUARD_STATE_H_
#define COMPONENTS_AERIE_ADBLOCK_BROWSER_GUARD_STATE_H_

#include <string>

#include "components/aerie_adblock/browser/session_key.h"

class GURL;

namespace aerie_adblock {

class FilterListManager;

// What the Aerie Guard icon in the address bar shows for one tab (issue #99).
struct GuardState {
  // The icon shows only on http and https pages.
  bool visible = false;
  // Blocking is off for the page's site (a saved exception, or one of this
  // session). The icon takes its "off" look.
  bool blocking_off = false;
  // The tab's session cannot change the exception: it is an incognito session
  // and the regular profile saved the exception (FilterListManager::
  // IsExceptionLocked()).
  bool locked = false;
  // Cosmetic hiding ("Hide leftover ad boxes") is off for the page's site
  // while blocking may stay on (a saved choice, or one of this session). Issue
  // #101.
  bool cosmetic_off = false;
  // The tab's session cannot turn cosmetic hiding back on: it is an incognito
  // session and the regular profile saved the choice.
  bool cosmetic_locked = false;
  // The host of the page, as exceptions key it. Empty when not |visible|.
  std::string host;
  // Requests blocked on the page. 0 while blocking is off.
  int count = 0;

  friend bool operator==(const GuardState&, const GuardState&) = default;
};

// The text drawn on the icon for |count| blocked requests: "" for 0 or less,
// the number from 1 to 99, "99+" above.
std::u16string GuardBadgeLabel(int count);

// The state of the icon. Reads only browser-side state: |committed_url| is the
// committed URL of the tab's primary main frame (never a URL the renderer
// proposes), |session| the tab's profile (SessionKeyFor()), |manager| the
// exceptions (null before the browser's main parts start it: then blocking is
// on) and |blocked_count| what PageBlockCounter counted.
GuardState ComputeGuardState(const GURL& committed_url,
                             SessionKey session,
                             const FilterListManager* manager,
                             int blocked_count);

}  // namespace aerie_adblock

#endif  // COMPONENTS_AERIE_ADBLOCK_BROWSER_GUARD_STATE_H_
