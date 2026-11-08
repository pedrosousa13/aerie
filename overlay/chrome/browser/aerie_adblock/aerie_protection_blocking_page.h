// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_AERIE_ADBLOCK_AERIE_PROTECTION_BLOCKING_PAGE_H_
#define CHROME_BROWSER_AERIE_ADBLOCK_AERIE_PROTECTION_BLOCKING_PAGE_H_

#include <memory>
#include <string>

#include "base/memory/raw_ptr.h"
#include "components/security_interstitials/content/security_interstitial_controller_client.h"
#include "components/security_interstitials/content/security_interstitial_page.h"
#include "url/gurl.h"

namespace content {
class WebContents;
}  // namespace content

// What the buttons of the interstitial do.
class AerieProtectionControllerClient
    : public security_interstitials::SecurityInterstitialControllerClient {
 public:
  AerieProtectionControllerClient(content::WebContents* web_contents,
                                  const GURL& request_url);
  AerieProtectionControllerClient(const AerieProtectionControllerClient&) =
      delete;
  AerieProtectionControllerClient& operator=(
      const AerieProtectionControllerClient&) = delete;
  ~AerieProtectionControllerClient() override;

  // security_interstitials::ControllerClient:
  void GoBack() override;
  void Proceed() override;

 private:
  raw_ptr<content::WebContents> web_contents_;
  const std::string host_;
};

// The interstitial for a main-frame navigation to a listed phishing or
// malware domain. Aerie's own page (the blocked site's content is never
// fetched, so it cannot script or style this page), built from Chromium's
// shared interstitial template in its safe browsing look.
//
// "Go back" is the primary button. "Continue anyway" is the link in the
// details section, which is closed until the user opens it.
class AerieProtectionBlockingPage
    : public security_interstitials::SecurityInterstitialPage {
 public:
  AerieProtectionBlockingPage(
      content::WebContents* web_contents,
      const GURL& request_url,
      std::unique_ptr<security_interstitials::SecurityInterstitialControllerClient>
          controller_client);
  ~AerieProtectionBlockingPage() override;

  static const SecurityInterstitialPage::TypeID kTypeForTesting;

  // security_interstitials::SecurityInterstitialPage:
  void OnInterstitialClosing() override {}
  SecurityInterstitialPage::TypeID GetTypeForTesting() override;

 protected:
  void CommandReceived(const std::string& command) override;
  void PopulateInterstitialStrings(base::DictValue& load_time_data) override;
};

#endif  // CHROME_BROWSER_AERIE_ADBLOCK_AERIE_PROTECTION_BLOCKING_PAGE_H_
