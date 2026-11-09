// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_ADBLOCK_BROWSER_GUARD_TARGET_H_
#define COMPONENTS_AERIE_ADBLOCK_BROWSER_GUARD_TARGET_H_

#include <optional>
#include <string>

#include "base/memory/weak_ptr.h"
#include "components/aerie_adblock/browser/guard_state.h"

namespace content {
class WebContents;
}  // namespace content

namespace aerie_adblock {

// The Guard state of |tab| now: the committed URL of its primary main frame,
// the exceptions of FilterListManager and the PageBlockCounter's count. A tab
// that is null shows nothing (|visible| is false). Browser-side state only.
GuardState GuardStateOfTab(content::WebContents* tab);

// The one tab and site that a Guard panel acts on (issue #100). The browser
// captures it when the panel opens, from the browser's own state; the panel's
// page never names a tab, a host or a URL.
//
// Every use goes through Resolve(): the target is good only while the tab is
// alive and its primary main frame still shows the captured host. After a
// navigation to another site, or when the tab closes, nothing is changed and
// the caller closes the panel.
//
// An empty target (no tab, or a tab that shows no http or https page) never
// resolves. A panel that has no target, like chrome://aerie-guard.top-chrome
// loaded in an ordinary tab, can read nothing and change nothing.
class GuardTarget {
 public:
  enum class Toggle {
    kApplied,   // The setting changed and the tab reloads.
    kStale,     // The tab is gone or shows another site. Nothing changed.
    kRefused,   // The session cannot make this change. Nothing changed.
  };

  // An empty target.
  GuardTarget();
  // The site |tab| shows now. Empty if |tab| is null or shows no http(s) page.
  static GuardTarget Capture(content::WebContents* tab);

  GuardTarget(const GuardTarget&);
  GuardTarget& operator=(const GuardTarget&);
  ~GuardTarget();

  bool empty() const { return host_.empty(); }
  const std::string& host() const { return host_; }
  // The tab, or null when it is gone. Does not check the host: use Resolve().
  content::WebContents* tab() const { return tab_.get(); }

  // The state of the captured site, or nothing if the target is empty, the tab
  // is gone, or the tab now shows another host.
  std::optional<GuardState> Resolve() const;

  // Turns blocking on or off for the captured site and its subdomains (the
  // exception covers both), then reloads the tab. For an off-the-record tab the
  // change is in memory only (FilterListManager::SetException). Does nothing
  // unless Resolve() succeeds and the session may make the change (a saved
  // exception holds in an incognito tab: that is kRefused).
  Toggle SetBlockingOn(bool blocking_on) const;

  // Turns cosmetic hiding ("Hide leftover ad boxes") on or off for the
  // captured site and its subdomains, then reloads the tab, because a sheet that
  // is already in the page stays until the next navigation (issue #101).
  // Network blocking is not touched. Same rules as SetBlockingOn(): nothing
  // happens unless Resolve() succeeds, an off-the-record tab keeps the change
  // in memory, and it cannot undo a choice the regular profile saved (kRefused).
  Toggle SetCosmeticHidingOn(bool hiding_on) const;

 private:
  base::WeakPtr<content::WebContents> tab_;
  std::string host_;
};

}  // namespace aerie_adblock

#endif  // COMPONENTS_AERIE_ADBLOCK_BROWSER_GUARD_TARGET_H_
