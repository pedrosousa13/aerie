// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_DESIGN_TOKENS_AERIE_MOTION_PREFS_H_
#define COMPONENTS_AERIE_DESIGN_TOKENS_AERIE_MOTION_PREFS_H_

class PrefRegistrySimple;

namespace aerie::motion {

// The in-app reduced-motion setting (issue #174). A profile pref with two
// values: "system" (the default) follows the operating system, "on" always
// reduces motion. The Appearance page of Settings writes it through
// settingsPrivate; the name is also a string literal in the settings_private
// allowlist (patches/sidebar/0001) and in the page (aerie_motion_row.ts), and
// a test compares the three.
inline constexpr char kReduceMotionPref[] = "aerie.motion.reduce";
inline constexpr char kReduceMotionSystem[] = "system";
inline constexpr char kReduceMotionOn[] = "on";

// Called from chrome::RegisterProfilePrefs (patches/sidebar/0001).
void RegisterProfilePrefs(PrefRegistrySimple* registry);

}  // namespace aerie::motion

#endif  // COMPONENTS_AERIE_DESIGN_TOKENS_AERIE_MOTION_PREFS_H_
