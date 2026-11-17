// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_sidebar/aerie_sidebar_layout_lock.h"

#include <map>

#include "base/memory/scoped_refptr.h"
#include "base/no_destructor.h"
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

namespace {

// The registered stores. A store is kept alive by its reference here too, a
// few bytes for each profile folder of the run.
std::map<base::FilePath, scoped_refptr<ValueMapPrefStore>>& Stores() {
  static base::NoDestructor<
      std::map<base::FilePath, scoped_refptr<ValueMapPrefStore>>>
      stores;
  return *stores;
}

}  // namespace

void SetLockedLayout(ValueMapPrefStore* store, TabLayout layout) {
  store->SetValue(kHeliumLayoutPref, base::Value(HeliumLayoutFor(layout)),
                  WriteablePrefStore::DEFAULT_PREF_WRITE_FLAGS);
  store->SetValue(kHeliumZenModePref, base::Value(false),
                  WriteablePrefStore::DEFAULT_PREF_WRITE_FLAGS);
}

void RegisterLayoutLockStore(const base::FilePath& profile_path,
                             ValueMapPrefStore* store) {
  Stores()[profile_path] = store;
}

void UnregisterLayoutLockStore(const base::FilePath& profile_path) {
  Stores().erase(profile_path);
}

bool FollowTabLayout(const base::FilePath& profile_path, TabLayout layout) {
  auto it = Stores().find(profile_path);
  if (it == Stores().end()) {
    return false;
  }
  SetLockedLayout(it->second.get(), layout);
  return true;
}

}  // namespace aerie_sidebar
