// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_AERIE_FILTERS_AERIE_FILTERS_UI_H_
#define CHROME_BROWSER_UI_WEBUI_AERIE_FILTERS_AERIE_FILTERS_UI_H_

#include "content/public/browser/web_ui_controller.h"
#include "content/public/browser/webui_config.h"
#include "content/public/common/url_constants.h"

namespace aerie_adblock {

inline constexpr char kAerieFiltersHost[] = "aerie-filters";
inline constexpr char kAerieFiltersURL[] = "chrome://aerie-filters/";

class AerieFiltersUI;

// chrome://aerie-filters: where the user manages filter lists, per-site
// exceptions and the update schedule (issue #10). Registered in
// chrome_web_ui_configs.cc by patches/adblock/0009.
class AerieFiltersUIConfig : public content::DefaultWebUIConfig<AerieFiltersUI> {
 public:
  AerieFiltersUIConfig()
      : DefaultWebUIConfig(content::kChromeUIScheme, kAerieFiltersHost) {}
};

class AerieFiltersUI : public content::WebUIController {
 public:
  explicit AerieFiltersUI(content::WebUI* web_ui);
  ~AerieFiltersUI() override;
  AerieFiltersUI(const AerieFiltersUI&) = delete;
  AerieFiltersUI& operator=(const AerieFiltersUI&) = delete;
};

}  // namespace aerie_adblock

#endif  // CHROME_BROWSER_UI_WEBUI_AERIE_FILTERS_AERIE_FILTERS_UI_H_
