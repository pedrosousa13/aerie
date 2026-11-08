// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/ui/webui/aerie_onboarding/aerie_theme_handler.h"

#include <string>

#include "base/functional/bind.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/themes/theme_service_factory.h"
#include "content/public/browser/web_ui.h"
#include "ui/base/mojom/themes.mojom.h"

namespace aerie_onboarding {

namespace {

// The single string argument of a message, or nullptr.
const std::string* OnlyString(const base::ListValue& args) {
  if (args.size() != 1 || !args[0].is_string()) {
    return nullptr;
  }
  return &args[0].GetString();
}

}  // namespace

AerieThemeHandler::AerieThemeHandler(Profile* profile) : profile_(profile) {}

AerieThemeHandler::~AerieThemeHandler() = default;

void AerieThemeHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback(
      "aerieSetup.setColorScheme",
      base::BindRepeating(&AerieThemeHandler::HandleSetColorScheme,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "aerieSetup.setThemeColor",
      base::BindRepeating(&AerieThemeHandler::HandleSetThemeColor,
                          base::Unretained(this)));
}

void AerieThemeHandler::HandleSetColorScheme(const base::ListValue& args) {
  const std::string* value = OnlyString(args);
  if (!value) {
    return;
  }
  ThemeService::BrowserColorScheme scheme;
  if (*value == "light") {
    scheme = ThemeService::BrowserColorScheme::kLight;
  } else if (*value == "dark") {
    scheme = ThemeService::BrowserColorScheme::kDark;
  } else if (*value == "system") {
    scheme = ThemeService::BrowserColorScheme::kSystem;
  } else {
    return;
  }
  ThemeServiceFactory::GetForProfile(profile_)->SetBrowserColorScheme(scheme);
}

void AerieThemeHandler::HandleSetThemeColor(const base::ListValue& args) {
  const std::string* value = OnlyString(args);
  if (!value) {
    return;
  }
  for (const ThemeChoice& choice : kThemeChoices) {
    if (choice.id != *value) {
      continue;
    }
    ThemeService* themes = ThemeServiceFactory::GetForProfile(profile_);
    if (choice.baseline) {
      // SetIsGrayscale(false) clears the user colour and the grayscale pref
      // and leaves the baseline colour source. UseDefaultTheme() is not used:
      // it sets the grayscale pref back to true, which is Graphite.
      themes->SetIsGrayscale(false);
    } else if (choice.seed) {
      themes->SetIsGrayscale(false);
      themes->SetUserColorAndBrowserColorVariant(
          *choice.seed, ui::mojom::BrowserColorVariant::kTonalSpot);
    } else {
      themes->SetIsGrayscale(true);
    }
    return;
  }
}

}  // namespace aerie_onboarding
