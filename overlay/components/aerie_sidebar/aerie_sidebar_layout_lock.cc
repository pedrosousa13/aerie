// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_sidebar/aerie_sidebar_layout_lock.h"

#include "base/values.h"
#include "components/aerie_sidebar/aerie_sidebar_layout.h"
#include "components/prefs/pref_store.h"
#include "components/prefs/value_map_pref_store.h"

namespace aerie_sidebar {

void LockHeliumLayout(ValueMapPrefStore* store) {
  store->SetValueSilently(kHeliumLayoutPref, base::Value(kHeliumLayoutVertical),
                          WriteablePrefStore::DEFAULT_PREF_WRITE_FLAGS);
  store->SetValueSilently(kHeliumZenModePref, base::Value(false),
                          WriteablePrefStore::DEFAULT_PREF_WRITE_FLAGS);
}

}  // namespace aerie_sidebar
