// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_TAB_MODEL_TAB_MODEL_PREFS_H_
#define COMPONENTS_AERIE_TAB_MODEL_TAB_MODEL_PREFS_H_

#include <optional>
#include <string_view>

#include "base/time/time.h"

class PrefRegistrySimple;
class PrefService;

namespace aerie_tab_model {

// How long a Today tab stays before it is archived: "12h" (default), "24h",
// "7d" or "never". A profile pref. The names are also string literals in the
// settings_private allowlist (patches/sidebar/0010), and a test compares them.
inline constexpr char kArchiveAfter[] = "aerie.tabs.archive_after";
// Minutes of idle time before a tab sleeps: 15 to 480, or 0 for never. Default
// 60.
inline constexpr char kSleepAfterMinutes[] = "aerie.tabs.sleep_after_minutes";

inline constexpr char kDefaultArchiveAfter[] = "12h";
inline constexpr int kDefaultSleepAfterMinutes = 60;
inline constexpr int kMinSleepAfterMinutes = 15;
inline constexpr int kMaxSleepAfterMinutes = 8 * 60;

// Called from chrome::RegisterProfilePrefs (patches/sidebar/0010).
void RegisterProfilePrefs(PrefRegistrySimple* registry);

// The archive delay for a pref value. nullopt means never. A value that is not
// one of the four falls back to the default of 12 hours.
std::optional<base::TimeDelta> ParseArchiveAfter(std::string_view value);

// The sleep delay for a pref value. nullopt means never (0). Other values are
// clamped to 15 minutes to 8 hours; a negative one means the default.
std::optional<base::TimeDelta> SleepAfterFromMinutes(int minutes);

// The same, read from the prefs.
std::optional<base::TimeDelta> GetArchiveAfter(const PrefService& prefs);
std::optional<base::TimeDelta> GetSleepAfter(const PrefService& prefs);

}  // namespace aerie_tab_model

#endif  // COMPONENTS_AERIE_TAB_MODEL_TAB_MODEL_PREFS_H_
