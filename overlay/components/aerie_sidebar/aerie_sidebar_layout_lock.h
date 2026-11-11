// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_SIDEBAR_AERIE_SIDEBAR_LAYOUT_LOCK_H_
#define COMPONENTS_AERIE_SIDEBAR_AERIE_SIDEBAR_LAYOUT_LOCK_H_

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

}  // namespace aerie_sidebar

#endif  // COMPONENTS_AERIE_SIDEBAR_AERIE_SIDEBAR_LAYOUT_LOCK_H_
