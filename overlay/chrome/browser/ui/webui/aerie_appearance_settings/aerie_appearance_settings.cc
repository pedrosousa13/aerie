// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/ui/webui/aerie_appearance_settings/aerie_appearance_settings.h"

#include <string>

#include "components/aerie_design_tokens/aerie_tokens.h"
#include "components/aerie_frame/aerie_frame_look.h"
#include "content/public/browser/web_ui_data_source.h"

namespace aerie_appearance_settings {

void AddThemeSwatches(content::WebUIDataSource* source) {
  static constexpr const char* kKeys[aerie::tokens::kThemeCount] = {
      "aerieThemeSwatchVivid", "aerieThemeSwatchSoft",
      "aerieThemeSwatchClean", "aerieThemeSwatchOled",
      "aerieThemeSwatchHighContrast", "aerieThemeSwatchMono"};
  for (int i = 0; i < aerie::tokens::kThemeCount; ++i) {
    source->AddString(
        kKeys[i],
        aerie_frame::ThemeSwatchCss(static_cast<aerie::tokens::ThemeId>(i)));
  }
}

}  // namespace aerie_appearance_settings
