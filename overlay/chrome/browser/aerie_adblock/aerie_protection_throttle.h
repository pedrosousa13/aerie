// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_AERIE_ADBLOCK_AERIE_PROTECTION_THROTTLE_H_
#define CHROME_BROWSER_AERIE_ADBLOCK_AERIE_PROTECTION_THROTTLE_H_

#include <string>

#include "base/memory/weak_ptr.h"
#include "content/public/browser/navigation_throttle.h"

namespace content {
class NavigationThrottleRegistry;
}  // namespace content

// Judges every http(s) navigation, of any frame, against the protection list
// (phishing and malware domains) before the request goes out: at the start and
// again at each redirect hop, so a chain cannot hide a listed host behind an
// unlisted one. What happens to a listed host is decided by
// aerie_adblock::Decide() (components/aerie_adblock/browser/protection_policy.h):
// a main frame gets the interstitial, a subframe is blocked, a prerender is
// cancelled.
//
// The throttle runs in the browser process, before the response, so the
// blocked site never gets to run code or style the interstitial.
//
// Added by patches/adblock/0011-hook-protection-navigation-throttle.patch, first
// in ChromeContentBrowserClient::CreateThrottlesForNavigation. Does nothing
// while no ProtectionService is registered (unit tests, shutdown).
class AerieProtectionThrottle : public content::NavigationThrottle {
 public:
  static void MaybeCreateAndAdd(content::NavigationThrottleRegistry& registry);

  explicit AerieProtectionThrottle(content::NavigationThrottleRegistry& registry);
  AerieProtectionThrottle(const AerieProtectionThrottle&) = delete;
  AerieProtectionThrottle& operator=(const AerieProtectionThrottle&) = delete;
  ~AerieProtectionThrottle() override;

  // content::NavigationThrottle:
  ThrottleCheckResult WillStartRequest() override;
  ThrottleCheckResult WillRedirectRequest() override;
  const char* GetNameForLogging() override;

 private:
  ThrottleCheckResult Judge();
  void OnListed(std::string host, bool listed);
  ThrottleCheckResult InterstitialFor(const std::string& host);

  base::WeakPtrFactory<AerieProtectionThrottle> weak_factory_{this};
};

#endif  // CHROME_BROWSER_AERIE_ADBLOCK_AERIE_PROTECTION_THROTTLE_H_
