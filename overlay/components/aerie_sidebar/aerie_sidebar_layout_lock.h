// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_SIDEBAR_AERIE_SIDEBAR_LAYOUT_LOCK_H_
#define COMPONENTS_AERIE_SIDEBAR_AERIE_SIDEBAR_LAYOUT_LOCK_H_

#include "base/files/file_path.h"
#include "components/aerie_sidebar/aerie_sidebar_layout.h"

class ValueMapPrefStore;

namespace aerie_sidebar {

// Locks Helium's tab layout to the sidebar (issue #215, ADR 0008): puts
// "helium.browser.layout" = vertical and "helium.browser.zen_mode" = false
// into `store`, which must be the command-line layer of a PrefService. That
// layer is read before the user's own file and after nothing Aerie writes, so
// the two prefs have these values whatever the user store, Settings, sync or an
// old profile holds, and `PrefService::Preference::IsUserModifiable()` is false
// for them. Called once, when the store is built.
void LockHeliumLayout(ValueMapPrefStore* store);

// Issue #186: the lock follows aerie.sidebar.layout. The value is replaced with
// the one that goes with `layout` (HeliumLayoutFor), through SetValue, so that
// the observers of the pref hear of a real change; a layout the store holds
// already is no change. Zen mode stays off. `store` must be a store that
// LockHeliumLayout filled.
void SetLockedLayout(ValueMapPrefStore* store, TabLayout layout);

// A profile's lock is found by the path of its profile folder: the pref service
// factory registers the command-line store it makes (chrome_pref_service_factory.cc,
// patches/sidebar/0051), and the code that watches aerie.sidebar.layout moves it.
// Only one store is kept per path, and a new one replaces the old one. Call on
// the UI thread.
void RegisterLayoutLockStore(const base::FilePath& profile_path,
                             ValueMapPrefStore* store);
void UnregisterLayoutLockStore(const base::FilePath& profile_path);

// Moves the locked layout of the profile at `profile_path` to `layout`. False
// when no store is registered for the path (nothing is changed).
bool FollowTabLayout(const base::FilePath& profile_path, TabLayout layout);

}  // namespace aerie_sidebar

#endif  // COMPONENTS_AERIE_SIDEBAR_AERIE_SIDEBAR_LAYOUT_LOCK_H_
