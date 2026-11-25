// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_AERIE_ONBOARDING_AERIE_SETUP_HANDLER_H_
#define CHROME_BROWSER_UI_WEBUI_AERIE_ONBOARDING_AERIE_SETUP_HANDLER_H_

#include <optional>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/values.h"
#include "content/public/browser/web_ui_message_handler.h"

class PrefService;

namespace aerie_onboarding {

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
//   aerieSetup.getBrowserIcons
//                        [callbackId, [<browser name>, ...]] (at most 64 names)
//                        Answers {<name>: "data:image/png;base64,..."} for the
//                        names that have an icon on this computer, read from the
//                        browser's .desktop entry and the icon theme (issue #91).
//                        The names are only lookup keys in a fixed table, never a
//                        path. It reads local files and never touches the network.
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

  // Looks the icons up in `dirs` (directories shaped like /usr/share) instead of the
  // system's.
  void set_icon_data_dirs_for_testing(std::vector<base::FilePath> dirs) {
    icon_data_dirs_ = std::move(dirs);
  }

 private:
  void HandleSetPref(const base::ListValue& args);
  void HandleAcceptLatestSchema(const base::ListValue& args);
  void HandleGetBrowserIcons(const base::ListValue& args);
  void OnBrowserIcons(const std::string& callback_id, base::DictValue icons);

  raw_ptr<PrefService> prefs_;
  std::optional<std::vector<base::FilePath>> icon_data_dirs_;
  base::WeakPtrFactory<AerieSetupHandler> weak_factory_{this};
};

}  // namespace aerie_onboarding

#endif  // CHROME_BROWSER_UI_WEBUI_AERIE_ONBOARDING_AERIE_SETUP_HANDLER_H_
