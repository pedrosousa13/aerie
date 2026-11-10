// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_sidebar/aerie_sidebar_layout.h"

#include <algorithm>

#include "components/aerie_design_tokens/aerie_tokens.h"
#include "components/aerie_sidebar/aerie_sidebar_prefs.h"
#include "components/prefs/pref_service.h"

namespace aerie_sidebar {

bool IsSidebarLayout(const PrefService& prefs) {
  return prefs.GetString(kLayoutPref) != kLayoutHorizontal;
}

void EnforceHeliumLayout(PrefService* prefs) {
  if (!IsSidebarLayout(*prefs)) {
    return;
  }
  if (prefs->GetInteger(kHeliumLayoutPref) != kHeliumLayoutVertical) {
    prefs->SetInteger(kHeliumLayoutPref, kHeliumLayoutVertical);
  }
  if (prefs->GetBoolean(kHeliumZenModePref)) {
    prefs->SetBoolean(kHeliumZenModePref, false);
  }
}

int ClampWidth(int width) {
  return std::clamp(width, aerie::tokens::kSizeSidebarMin,
                    aerie::tokens::kSizeSidebarMax);
}

int GetWidth(const PrefService& prefs) {
  const int stored = prefs.GetInteger(kWidthPref);
  return stored <= 0 ? aerie::tokens::kSizeSidebarWidth : ClampWidth(stored);
}

int ResizeByKey(int width, bool grow) {
  return ClampWidth(width + (grow ? kKeyboardResizeStep : -kKeyboardResizeStep));
}

bool IsVisible(const PrefService& prefs) {
  return prefs.GetBoolean(kVisiblePref);
}

bool ToggleVisible(PrefService* prefs) {
  const bool visible = !IsVisible(*prefs);
  prefs->SetBoolean(kVisiblePref, visible);
  return visible;
}

int ResizeByDrag(int start_width, int delta_x, bool sidebar_on_right) {
  return ClampWidth(start_width + (sidebar_on_right ? -delta_x : delta_x));
}

}  // namespace aerie_sidebar
