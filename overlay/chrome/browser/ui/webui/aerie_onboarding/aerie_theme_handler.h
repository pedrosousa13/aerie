// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_AERIE_ONBOARDING_AERIE_THEME_HANDLER_H_
#define CHROME_BROWSER_UI_WEBUI_AERIE_ONBOARDING_AERIE_THEME_HANDLER_H_

#include <optional>
#include <string_view>

#include "base/memory/raw_ptr.h"
#include "base/values.h"
#include "content/public/browser/web_ui_message_handler.h"
#include "third_party/skia/include/core/SkColor.h"

class Profile;

namespace aerie_onboarding {

// The colours the setup page offers, by id. The page sends an id, never a raw
// colour, so a page script cannot set an arbitrary theme. A choice with a seed
// colour is a coloured theme. Neutral has no seed: it is the plain default, no
// user colour and the grayscale pref on (Helium's default for a new profile).
// It follows the light, dark or system appearance, so one entry serves both
// modes.
struct ThemeChoice {
  std::string_view id;
  std::optional<SkColor> seed;
};
inline constexpr ThemeChoice kThemeChoices[] = {
    {"neutral", std::nullopt},
    {"alpenglow", SkColorSetRGB(0xF0, 0x46, 0x6F)},
    {"sky", SkColorSetRGB(0x2F, 0x6B, 0xFF)},
    {"moss", SkColorSetRGB(0x13, 0x84, 0x76)},
    {"dusk", SkColorSetRGB(0x8E, 0x4F, 0xE8)},
    {"sand", SkColorSetRGB(0xD9, 0x9A, 0x6C)},
};

// Applies the "Make it yours" step of aerie://setup (issue #46): light, dark
// or system colours, and one of kThemeChoices. Both are ordinary theme
// settings, so the user can change them later in Settings > Appearance.
//
// Messages:
//   aerieSetup.setColorScheme  ["light" | "dark" | "system"]
//   aerieSetup.setThemeColor   [<id from kThemeChoices>]
// Anything else is ignored.
class AerieThemeHandler : public content::WebUIMessageHandler {
 public:
  explicit AerieThemeHandler(Profile* profile);
  AerieThemeHandler(const AerieThemeHandler&) = delete;
  AerieThemeHandler& operator=(const AerieThemeHandler&) = delete;
  ~AerieThemeHandler() override;

  // content::WebUIMessageHandler:
  void RegisterMessages() override;

 private:
  void HandleSetColorScheme(const base::ListValue& args);
  void HandleSetThemeColor(const base::ListValue& args);

  raw_ptr<Profile> profile_;
};

}  // namespace aerie_onboarding

#endif  // CHROME_BROWSER_UI_WEBUI_AERIE_ONBOARDING_AERIE_THEME_HANDLER_H_
