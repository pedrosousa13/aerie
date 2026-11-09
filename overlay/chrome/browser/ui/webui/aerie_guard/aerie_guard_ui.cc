// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/ui/webui/aerie_guard/aerie_guard_ui.h"

#include <utility>

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/webui/aerie_guard/aerie_guard_handler.h"
#include "chrome/browser/ui/webui/aerie_guard/aerie_guard_panel_target.h"
#include "chrome/browser/ui/webui/favicon_source.h"
#include "chrome/common/webui_url_constants.h"
#include "chrome/grit/aerie_guard_resources.h"
#include "chrome/grit/aerie_guard_resources_map.h"
#include "chrome/grit/generated_resources.h"
#include "components/favicon_base/favicon_url_parser.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "content/public/common/url_constants.h"
#include "ui/base/webui/web_ui_util.h"
#include "ui/webui/webui_util.h"

namespace aerie_guard {

AerieGuardUIConfig::AerieGuardUIConfig()
    : DefaultTopChromeWebUIConfig(content::kChromeUIScheme,
                                  chrome::kChromeUIAerieGuardHost) {}

bool AerieGuardUIConfig::ShouldAutoResizeHost() {
  return true;
}

bool AerieGuardUIConfig::IsPreloadable() {
  return false;
}

// No chrome.send bindings (the base class default is off) and no message
// handler: the page talks to the browser through Mojo only.
AerieGuardUI::AerieGuardUI(content::WebUI* web_ui)
    : TopChromeWebUIController(web_ui) {
  Profile* profile = Profile::FromWebUI(web_ui);
  content::WebUIDataSource* source = content::WebUIDataSource::CreateAndAdd(
      profile, chrome::kChromeUIAerieGuardHost);
  // The default strict policy: trusted types, and script from this source
  // only. SetupWebUIDataSource adds nothing to it.
  webui::SetupWebUIDataSource(source, kAerieGuardResources,
                              IDR_AERIE_GUARD_AERIE_GUARD_HTML);

  static constexpr webui::LocalizedString kStrings[] = {
      {"title", IDS_AERIE_GUARD_PANEL_TITLE},
      {"switchLabel", IDS_AERIE_GUARD_PANEL_SWITCH_LABEL},
      {"statusOn", IDS_AERIE_GUARD_PANEL_STATUS_ON},
      {"statusOff", IDS_AERIE_GUARD_PANEL_STATUS_OFF},
      {"scope", IDS_AERIE_GUARD_PANEL_SCOPE},
      {"incognito", IDS_AERIE_GUARD_PANEL_INCOGNITO},
      {"locked", IDS_AERIE_GUARD_PANEL_LOCKED},
      {"noSite", IDS_AERIE_GUARD_PANEL_NO_SITE},
      {"advanced", IDS_AERIE_GUARD_PANEL_ADVANCED},
      {"filterLists", IDS_AERIE_GUARD_PANEL_FILTER_LISTS},
  };
  source->AddLocalizedStrings(kStrings);

  // The favicon of the site, from the local store.
  content::URLDataSource::Add(
      profile, std::make_unique<FaviconSource>(
                   profile, chrome::FaviconUrlFormat::kFavicon2));
}

AerieGuardUI::~AerieGuardUI() = default;

WEB_UI_CONTROLLER_TYPE_IMPL(AerieGuardUI)

void AerieGuardUI::BindInterface(
    mojo::PendingReceiver<mojom::PageHandlerFactory> receiver) {
  factory_receiver_.reset();
  factory_receiver_.Bind(std::move(receiver));
}

void AerieGuardUI::CreatePageHandler(
    mojo::PendingRemote<mojom::Page> page,
    mojo::PendingReceiver<mojom::PageHandler> handler) {
  // The target is the browser's capture of the tab and site the Guard icon
  // opened this panel for (empty when the page is in an ordinary tab).
  handler_ = std::make_unique<AerieGuardHandler>(
      std::move(handler), std::move(page),
      AerieGuardPanelTarget::Get(web_ui()->GetWebContents()), embedder());
}

}  // namespace aerie_guard
