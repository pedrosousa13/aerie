// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_adblock/browser/protection_policy.h"

#include "base/strings/string_util.h"

namespace aerie_adblock {

void SessionAllowlist::Add(std::string_view host) {
  if (!host.empty()) {
    hosts_.emplace(host);
  }
}

bool SessionAllowlist::Contains(std::string_view host) const {
  return hosts_.find(host) != hosts_.end();
}

std::optional<std::string> HostToCheck(const GURL& url) {
  if (!url.is_valid() || !url.SchemeIsHTTPOrHTTPS() || !url.has_host()) {
    return std::nullopt;
  }
  std::string_view host = url.host();
  if (host.empty() || !base::IsStringASCII(host)) {
    return std::nullopt;
  }
  return std::string(host);
}

NavigationVerdict Decide(bool host_listed,
                         std::string_view host,
                         NavigationKind kind,
                         const SessionAllowlist* allowlist) {
  if (!host_listed) {
    return NavigationVerdict::kAllow;
  }
  // The user's choice for this host, in this tab, in this session.
  if (allowlist && allowlist->Contains(host)) {
    return NavigationVerdict::kAllow;
  }
  switch (kind) {
    case NavigationKind::kOutermostMainFrame:
      return NavigationVerdict::kShowInterstitial;
    case NavigationKind::kSubframe:
      return NavigationVerdict::kBlockSubframe;
    case NavigationKind::kPrerenderedMainFrame:
      return NavigationVerdict::kCancel;
  }
  return NavigationVerdict::kCancel;
}

}  // namespace aerie_adblock
