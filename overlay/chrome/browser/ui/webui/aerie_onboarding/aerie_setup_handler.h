// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_AERIE_ONBOARDING_AERIE_SETUP_HANDLER_H_
#define CHROME_BROWSER_UI_WEBUI_AERIE_ONBOARDING_AERIE_SETUP_HANDLER_H_

#include "base/memory/raw_ptr.h"
#include "base/values.h"
#include "content/public/browser/web_ui_message_handler.h"

class PrefService;

namespace aerie_onboarding {

class AerieSetupHandlerBrowserTest;

// What aerie://setup may ask the browser to do besides the import, search engine,
// default browser and theme steps (issue #69). It replaces Helium's
// OnboardingMessageHandler, whose setPref sets any helium.* pref (including
// helium.services.user_consented) and which also installs extensions.
//
// Messages:
//   setPref              [callbackId, "completed_onboarding", <bool>]
//                        Any other name, or a value that is not a bool, is rejected
//                        and changes nothing.
//   acceptLatestSchema   []
// Nothing else is registered: getExtensions and installExtension do not exist for
// this page.
class AerieSetupHandler : public content::WebUIMessageHandler {
 public:
  explicit AerieSetupHandler(PrefService* prefs);
  AerieSetupHandler(const AerieSetupHandler&) = delete;
  AerieSetupHandler& operator=(const AerieSetupHandler&) = delete;
  ~AerieSetupHandler() override;

  // content::WebUIMessageHandler:
  void RegisterMessages() override;

 private:
  friend class AerieSetupHandlerBrowserTest;

  void HandleSetPref(const base::ListValue& args);
  void HandleAcceptLatestSchema(const base::ListValue& args);

  raw_ptr<PrefService> prefs_;
};

}  // namespace aerie_onboarding

#endif  // CHROME_BROWSER_UI_WEBUI_AERIE_ONBOARDING_AERIE_SETUP_HANDLER_H_
