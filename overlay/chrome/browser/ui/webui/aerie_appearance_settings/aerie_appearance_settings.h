// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_AERIE_APPEARANCE_SETTINGS_AERIE_APPEARANCE_SETTINGS_H_
#define CHROME_BROWSER_UI_WEBUI_AERIE_APPEARANCE_SETTINGS_AERIE_APPEARANCE_SETTINGS_H_

namespace content {
class WebUIDataSource;
}

namespace aerie_appearance_settings {

// Hands the Settings page the preview swatch of every theme, one string per
// theme (issue #185): aerieThemeSwatchVivid, aerieThemeSwatchSoft,
// aerieThemeSwatchClean, aerieThemeSwatchOled, aerieThemeSwatchHighContrast and
// aerieThemeSwatchMono, each a CSS background value made from the tokens
// (aerie_frame::ThemeSwatchCss), so the page holds no colour of its own.
void AddThemeSwatches(content::WebUIDataSource* source);

}  // namespace aerie_appearance_settings

#endif  // CHROME_BROWSER_UI_WEBUI_AERIE_APPEARANCE_SETTINGS_AERIE_APPEARANCE_SETTINGS_H_
