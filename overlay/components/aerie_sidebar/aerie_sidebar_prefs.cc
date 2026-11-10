// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_sidebar/aerie_sidebar_prefs.h"

#include "components/aerie_design_tokens/aerie_tokens.h"
#include "components/prefs/pref_registry_simple.h"

namespace aerie_sidebar {

void RegisterProfilePrefs(PrefRegistrySimple* registry) {
  registry->RegisterStringPref(kLayoutPref, kLayoutSidebar);
  registry->RegisterIntegerPref(kWidthPref,
                                aerie::tokens::kSizeSidebarWidth);
  registry->RegisterBooleanPref(kVisiblePref, true);
}

}  // namespace aerie_sidebar
