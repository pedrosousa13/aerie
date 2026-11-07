// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/ui/webui/aerie_filters/aerie_filters_ui.h"

#include <memory>

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/webui/aerie_filters/aerie_filters_handler.h"
#include "chrome/browser/ui/webui/aerie_filters/grit/aerie_filters_resources.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"

namespace aerie_adblock {

AerieFiltersUI::AerieFiltersUI(content::WebUI* web_ui)
    : content::WebUIController(web_ui) {
  content::WebUIDataSource* source = content::WebUIDataSource::CreateAndAdd(
      Profile::FromWebUI(web_ui), kAerieFiltersHost);
  source->AddResourcePath("aerie_filters.js", IDR_AERIE_FILTERS_JS);
  source->AddResourcePath("aerie_filters.css", IDR_AERIE_FILTERS_CSS);
  source->SetDefaultResource(IDR_AERIE_FILTERS_HTML);
  // The page runs only its own script ('self'); the default policy already
  // forbids inline script and every other origin.
  web_ui->AddMessageHandler(std::make_unique<AerieFiltersHandler>());
}

AerieFiltersUI::~AerieFiltersUI() = default;

}  // namespace aerie_adblock
