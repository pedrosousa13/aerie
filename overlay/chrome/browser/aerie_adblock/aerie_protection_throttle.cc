// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/aerie_adblock/aerie_protection_throttle.h"

#include <memory>
#include <optional>
#include <utility>

#include "base/functional/bind.h"
#include "base/metrics/histogram_functions.h"
#include "chrome/browser/aerie_adblock/aerie_protection_blocking_page.h"
#include "chrome/browser/aerie_adblock/aerie_protection_tab_state.h"
#include "components/aerie_adblock/browser/protection_policy.h"
#include "components/aerie_adblock/browser/protection_service.h"
#include "components/security_interstitials/content/security_interstitial_tab_helper.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/navigation_throttle_registry.h"
#include "content/public/browser/web_contents.h"
#include "net/base/net_errors.h"

namespace {

aerie_adblock::NavigationKind KindOf(content::NavigationHandle* handle) {
  if (handle->IsInPrerenderedMainFrame()) {
    return aerie_adblock::NavigationKind::kPrerenderedMainFrame;
  }
  if (handle->IsInOutermostMainFrame()) {
    return aerie_adblock::NavigationKind::kOutermostMainFrame;
  }
  return aerie_adblock::NavigationKind::kSubframe;
}

}  // namespace

// static
void AerieProtectionThrottle::MaybeCreateAndAdd(
    content::NavigationThrottleRegistry& registry) {
  registry.AddThrottle(std::make_unique<AerieProtectionThrottle>(registry));
}

AerieProtectionThrottle::AerieProtectionThrottle(
    content::NavigationThrottleRegistry& registry)
    : content::NavigationThrottle(registry) {}

AerieProtectionThrottle::~AerieProtectionThrottle() = default;

content::NavigationThrottle::ThrottleCheckResult
AerieProtectionThrottle::WillStartRequest() {
  return Judge();
}

content::NavigationThrottle::ThrottleCheckResult
AerieProtectionThrottle::WillRedirectRequest() {
  // GetURL() is the redirect target here, judged before the hop goes out.
  return Judge();
}

const char* AerieProtectionThrottle::GetNameForLogging() {
  return "AerieProtectionThrottle";
}

content::NavigationThrottle::ThrottleCheckResult AerieProtectionThrottle::Judge() {
  std::optional<std::string> host =
      aerie_adblock::HostToCheck(navigation_handle()->GetURL());
  if (!host) {
    return PROCEED;
  }
  aerie_adblock::ProtectionService* service =
      aerie_adblock::ProtectionService::Get();
  if (!service) {
    return PROCEED;
  }
  // The service answers after the list has loaded, so a navigation that starts
  // while the browser is starting up waits instead of passing unchecked.
  std::string host_value = *host;
  service->IsListed(
      std::move(host_value),
      base::BindOnce(&AerieProtectionThrottle::OnListed,
                     weak_factory_.GetWeakPtr(), *host));
  return DEFER;
}

void AerieProtectionThrottle::OnListed(std::string host, bool listed) {
  content::NavigationHandle* handle = navigation_handle();
  const aerie_adblock::SessionAllowlist* allowlist = nullptr;
  if (content::WebContents* contents = handle->GetWebContents()) {
    if (AerieProtectionTabState* state =
            AerieProtectionTabState::FromWebContents(contents)) {
      allowlist = &state->allowlist();
    }
  }
  const aerie_adblock::NavigationVerdict verdict =
      aerie_adblock::Decide(listed, host, KindOf(handle), allowlist);
  base::UmaHistogramBoolean("Aerie.Protection.NavigationBlocked",
                            verdict != aerie_adblock::NavigationVerdict::kAllow);
  switch (verdict) {
    case aerie_adblock::NavigationVerdict::kAllow:
      Resume();
      return;
    case aerie_adblock::NavigationVerdict::kShowInterstitial:
      CancelDeferredNavigation(InterstitialFor(host));
      return;
    case aerie_adblock::NavigationVerdict::kBlockSubframe:
      CancelDeferredNavigation(BLOCK_REQUEST);
      return;
    case aerie_adblock::NavigationVerdict::kCancel:
      CancelDeferredNavigation(CANCEL);
      return;
  }
}

content::NavigationThrottle::ThrottleCheckResult
AerieProtectionThrottle::InterstitialFor(const std::string& host) {
  content::NavigationHandle* handle = navigation_handle();
  content::WebContents* contents = handle->GetWebContents();
  if (!contents) {
    return BLOCK_REQUEST;
  }
  auto page = std::make_unique<AerieProtectionBlockingPage>(
      contents, handle->GetURL(),
      std::make_unique<AerieProtectionControllerClient>(contents,
                                                        handle->GetURL()));
  std::string html = page->GetHTMLContents();
  security_interstitials::SecurityInterstitialTabHelper::AssociateBlockingPage(
      handle, std::move(page));
  return ThrottleCheckResult(CANCEL, net::ERR_BLOCKED_BY_CLIENT,
                             std::move(html));
}
