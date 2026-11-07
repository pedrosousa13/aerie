// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/aerie_adblock/aerie_protection_blocking_page.h"

#include <utility>

#include "base/metrics/histogram_functions.h"
#include "base/notreached.h"
#include "base/strings/escape.h"
#include "base/strings/string_number_conversions.h"
#include "base/values.h"
#include "chrome/browser/aerie_adblock/aerie_protection_tab_state.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/webui_url_constants.h"
#include "components/security_interstitials/core/controller_client.h"
#include "components/security_interstitials/core/metrics_helper.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"

namespace {

std::unique_ptr<security_interstitials::MetricsHelper> MakeMetricsHelper(
    const GURL& url) {
  security_interstitials::MetricsHelper::ReportDetails settings;
  settings.metric_prefix = "aerie_protection";
  return std::make_unique<security_interstitials::MetricsHelper>(url, settings,
                                                                 nullptr);
}

}  // namespace

AerieProtectionControllerClient::AerieProtectionControllerClient(
    content::WebContents* web_contents,
    const GURL& request_url)
    : SecurityInterstitialControllerClient(
          web_contents,
          MakeMetricsHelper(request_url),
          Profile::FromBrowserContext(web_contents->GetBrowserContext())
              ->GetPrefs(),
          g_browser_process->GetApplicationLocale(),
          chrome::ChromeUINewTabURLAsGURL(),
          /*settings_page_helper=*/nullptr),
      web_contents_(web_contents),
      host_(request_url.host()) {}

AerieProtectionControllerClient::~AerieProtectionControllerClient() = default;

void AerieProtectionControllerClient::GoBack() {
  base::UmaHistogramBoolean("Aerie.Protection.UserProceeded", false);
  SecurityInterstitialControllerClient::GoBackAfterNavigationCommitted();
}

void AerieProtectionControllerClient::Proceed() {
  base::UmaHistogramBoolean("Aerie.Protection.UserProceeded", true);
  // Remember the host for this tab only, then load the page again. The
  // throttle sees the host in the tab's allowlist and lets the navigation
  // through. Nothing is written to disk.
  AerieProtectionTabState::GetOrCreate(web_contents_)
      ->allowlist()
      .Add(host_);
  web_contents_->GetController().Reload(content::ReloadType::NORMAL, false);
}

const security_interstitials::SecurityInterstitialPage::TypeID
    AerieProtectionBlockingPage::kTypeForTesting =
        &AerieProtectionBlockingPage::kTypeForTesting;

AerieProtectionBlockingPage::AerieProtectionBlockingPage(
    content::WebContents* web_contents,
    const GURL& request_url,
    std::unique_ptr<security_interstitials::SecurityInterstitialControllerClient>
        controller_client)
    : SecurityInterstitialPage(web_contents,
                               request_url,
                               std::move(controller_client)) {}

AerieProtectionBlockingPage::~AerieProtectionBlockingPage() = default;

security_interstitials::SecurityInterstitialPage::TypeID
AerieProtectionBlockingPage::GetTypeForTesting() {
  return AerieProtectionBlockingPage::kTypeForTesting;
}

void AerieProtectionBlockingPage::CommandReceived(const std::string& command) {
  int cmd = 0;
  if (!base::StringToInt(command, &cmd)) {
    return;  // e.g. "pageLoadComplete"; not a command
  }
  switch (cmd) {
    case security_interstitials::CMD_DONT_PROCEED:
      controller()->GoBack();
      break;
    case security_interstitials::CMD_PROCEED:
      controller()->Proceed();
      break;
    default:
      // The shared page can send other commands (show more, report, help).
      // This page supports none of them. A message from the page's renderer is
      // never trusted to do more than the two buttons.
      break;
  }
}

void AerieProtectionBlockingPage::PopulateInterstitialStrings(
    base::DictValue& load_time_data) {
  // The shared template needs these keys (see interstitial_large.js). Type
  // SAFEBROWSING gives the red look and the overridable "details" section.
  const std::string host = base::EscapeForHTML(request_url().host());
  load_time_data.Set("type", "SAFEBROWSING");
  load_time_data.Set("overridable", true);
  load_time_data.Set("hide_primary_button", false);
  load_time_data.Set("billing", false);
  load_time_data.Set("displaycheckbox", false);
  load_time_data.Set("boxchecked", false);
  load_time_data.Set("displayEnhancedProtectionMessage", false);
  load_time_data.Set("optInLink", "");
  load_time_data.Set("enhancedProtectionMessage", "");
  load_time_data.Set("tabTitle", "Dangerous site blocked");
  load_time_data.Set("heading", "Dangerous site blocked");
  load_time_data.Set(
      "primaryParagraph",
      "Aerie blocked <strong>" + host +
          "</strong> because it is on a list of phishing and malware sites. "
          "Attackers on sites like this can trick you into sharing passwords "
          "or installing harmful software.");
  load_time_data.Set("primaryButtonText", "Go back");
  load_time_data.Set("openDetails", "Details");
  load_time_data.Set("closeDetails", "Hide details");
  load_time_data.Set(
      "explanationParagraph",
      "Aerie checked this address on your computer, against a list from "
      "Phishing Army that Aerie ships. It sent nothing about this page to "
      "anyone. Lists can be wrong.");
  load_time_data.Set(
      "finalParagraph",
      "If you understand the risks, you can <a href=\"#\" "
      "id=\"proceed-link\">continue anyway</a>. Aerie remembers the choice "
      "for this tab only.");
}
