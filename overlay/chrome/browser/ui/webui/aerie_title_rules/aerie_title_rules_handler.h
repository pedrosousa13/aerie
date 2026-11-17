// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_AERIE_TITLE_RULES_AERIE_TITLE_RULES_HANDLER_H_
#define CHROME_BROWSER_UI_WEBUI_AERIE_TITLE_RULES_AERIE_TITLE_RULES_HANDLER_H_

#include <string_view>

#include "base/memory/raw_ptr.h"
#include "base/values.h"
#include "content/public/browser/web_ui_message_handler.h"

class Profile;

namespace aerie_title_rules {

// The "Try a title" box of the rename rules (issue #146), in the Sidebar
// section of Settings. It is added to the chrome://settings WebUI only
// (SettingsUI, patches/sidebar/0050). The page sends
//
//   sendWithPromise("aerieTitleRules.try", url, title)
//
// and the answer is {text, source, rule}: the title the sidebar would show for
// a tab with that address and page title, where it came from ("rule", "tidy" or
// "page") and the name of the rule when `source` is "rule". It runs the real
// resolver on the saved settings (the same code the sidebar runs), so the box
// cannot disagree with the sidebar, and it reads nothing but its two
// arguments: no page, no network.
//
// Both arguments are untrusted text: each must be a string of at most 4096
// bytes, or the promise is rejected with "bad_argument". The answer holds the
// page's own text and a rule name, so the page shows it as text only.
class AerieTitleRulesHandler : public content::WebUIMessageHandler {
 public:
  explicit AerieTitleRulesHandler(Profile* profile);
  ~AerieTitleRulesHandler() override;
  AerieTitleRulesHandler(const AerieTitleRulesHandler&) = delete;
  AerieTitleRulesHandler& operator=(const AerieTitleRulesHandler&) = delete;

  // content::WebUIMessageHandler:
  void RegisterMessages() override;

 private:
  void HandleTry(const base::ListValue& args);

  const raw_ptr<Profile> profile_;
};

}  // namespace aerie_title_rules

#endif  // CHROME_BROWSER_UI_WEBUI_AERIE_TITLE_RULES_AERIE_TITLE_RULES_HANDLER_H_
