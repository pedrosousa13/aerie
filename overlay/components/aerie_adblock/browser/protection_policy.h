// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_ADBLOCK_BROWSER_PROTECTION_POLICY_H_
#define COMPONENTS_AERIE_ADBLOCK_BROWSER_PROTECTION_POLICY_H_

#include <optional>
#include <set>
#include <string>
#include <string_view>

#include "url/gurl.h"

namespace aerie_adblock {

// Hosts the user chose "Continue anyway" for. Kept with one tab, in memory
// only: it is never written to disk and ends with the tab (so with the
// session at the latest). Exact hosts, not parent domains: continuing to
// a.evil.example does not open b.evil.example.
class SessionAllowlist {
 public:
  void Add(std::string_view host);
  bool Contains(std::string_view host) const;
  size_t size() const { return hosts_.size(); }

 private:
  std::set<std::string, std::less<>> hosts_;
};

// What a navigation needs to be judged. A navigation is judged at its start
// and again at every redirect hop (WillRedirectRequest), with the same
// function, so a chain cannot hide a listed host behind an unlisted one.
enum class NavigationKind {
  // A top-level page the user can see. Gets the interstitial.
  kOutermostMainFrame,
  // A subframe (or an embedded main frame). Blocked without buttons: the
  // frame fails with ERR_BLOCKED_BY_CLIENT.
  kSubframe,
  // A prerendered page nobody sees. Cancelled: no interstitial, no history.
  kPrerenderedMainFrame,
};

enum class NavigationVerdict {
  kAllow,
  kShowInterstitial,
  kBlockSubframe,
  kCancel,
};

// The host to look up for |url|, or nullopt when the URL is not judged: only
// http and https URLs with a plain ASCII host are (other schemes cannot reach
// a remote phishing site; chrome:, file:, data: and blob: are the browser's
// own business).
std::optional<std::string> HostToCheck(const GURL& url);

// The verdict for one navigation hop. |host_listed| is the protection engine's
// answer for |host|. |allowlist| may be null (a navigation with no tab).
NavigationVerdict Decide(bool host_listed,
                         std::string_view host,
                         NavigationKind kind,
                         const SessionAllowlist* allowlist);

}  // namespace aerie_adblock

#endif  // COMPONENTS_AERIE_ADBLOCK_BROWSER_PROTECTION_POLICY_H_
