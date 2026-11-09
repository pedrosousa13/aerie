// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_adblock/browser/aerie_guard_prefs.h"

#include "components/prefs/pref_registry_simple.h"

namespace aerie_guard {

void RegisterProfilePrefs(PrefRegistrySimple* registry) {
  registry->RegisterBooleanPref(kShowBlockedCount, true);
}

}  // namespace aerie_guard
