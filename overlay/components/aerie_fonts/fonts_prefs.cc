// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_fonts/fonts_prefs.h"

#include "components/prefs/pref_registry_simple.h"

namespace aerie_fonts {

void RegisterProfilePrefs(PrefRegistrySimple* registry) {
  registry->RegisterBooleanPref(kRestrictFonts, false);
}

}  // namespace aerie_fonts
