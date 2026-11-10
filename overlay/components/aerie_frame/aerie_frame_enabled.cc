// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_frame/aerie_frame_enabled.h"

#include "components/aerie_design_tokens/aerie_tokens.h"
#include "components/aerie_sidebar/aerie_sidebar_layout.h"

namespace aerie_frame {

bool IsFrameTintActive(const PrefService& prefs, bool is_normal_window) {
  return is_normal_window && aerie_sidebar::IsSidebarLayout(prefs);
}

int CardRadius() {
  return aerie::tokens::kRadiusXl;
}

}  // namespace aerie_frame
