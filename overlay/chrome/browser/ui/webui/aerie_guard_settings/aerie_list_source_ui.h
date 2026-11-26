// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_AERIE_GUARD_SETTINGS_AERIE_LIST_SOURCE_UI_H_
#define CHROME_BROWSER_UI_WEBUI_AERIE_GUARD_SETTINGS_AERIE_LIST_SOURCE_UI_H_

#include <string>

#include "content/public/browser/web_ui_controller.h"
#include "content/public/browser/webui_config.h"
#include "url/gurl.h"

namespace aerie_guard {

// aerie://list-source/<list id> (issue #108): the saved text of a filter list
// the user added by address, as plain text. Settings opens it for the "View
// source" button. The list is untrusted, so the page is a text/plain
// document and nothing else: no HTML, no script, no resources. The remote
// address is never opened. The page is a WebUI, so a web page cannot navigate
// to it or frame it. Registered in chrome_web_ui_configs.cc by
// patches/adblock/0030.
inline constexpr char kListSourceHost[] = "aerie-list-source";

// The address of the source page of the list |id|, or an empty URL when |id|
// is not a list id (so no address is ever made from other text).
GURL ListSourceUrl(const std::string& id);

class AerieListSourceUI;

class AerieListSourceUIConfig
    : public content::DefaultWebUIConfig<AerieListSourceUI> {
 public:
  AerieListSourceUIConfig();

  // content::WebUIConfig: only for a regular profile. Not for an incognito or
  // guest window: the page shows the saved lists, which are the regular
  // profile's.
  bool IsWebUIEnabled(content::BrowserContext* browser_context) override;
};

// The controller. No chrome.send bindings and no message handler; it adds the
// data source and nothing else.
class AerieListSourceUI : public content::WebUIController {
 public:
  explicit AerieListSourceUI(content::WebUI* web_ui);
  AerieListSourceUI(const AerieListSourceUI&) = delete;
  AerieListSourceUI& operator=(const AerieListSourceUI&) = delete;
  ~AerieListSourceUI() override;
};

}  // namespace aerie_guard

#endif  // CHROME_BROWSER_UI_WEBUI_AERIE_GUARD_SETTINGS_AERIE_LIST_SOURCE_UI_H_
