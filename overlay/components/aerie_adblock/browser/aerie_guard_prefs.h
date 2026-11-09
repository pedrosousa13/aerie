// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_ADBLOCK_BROWSER_AERIE_GUARD_PREFS_H_
#define COMPONENTS_AERIE_ADBLOCK_BROWSER_AERIE_GUARD_PREFS_H_

class PrefRegistrySimple;

namespace aerie_guard {

// Whether the Aerie Guard icon in the address bar draws the number of blocked
// items on itself (issue #103). A profile pref, on by default. The icon's
// tooltip states the count either way. The Aerie Guard page of Settings writes
// it through settingsPrivate; the name is also a string literal in the
// settings_private allowlist (patches/adblock/0022), and a test compares them.
inline constexpr char kShowBlockedCount[] = "aerie.guard.show_blocked_count";

// Called from chrome::RegisterProfilePrefs (patches/adblock/0022).
void RegisterProfilePrefs(PrefRegistrySimple* registry);

}  // namespace aerie_guard

#endif  // COMPONENTS_AERIE_ADBLOCK_BROWSER_AERIE_GUARD_PREFS_H_
