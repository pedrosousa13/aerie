// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_tab_model/tab_model_prefs.h"

#include <algorithm>

#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"

namespace aerie_tab_model {

void RegisterProfilePrefs(PrefRegistrySimple* registry) {
  registry->RegisterStringPref(kArchiveAfter, kDefaultArchiveAfter);
  registry->RegisterStringPref(kArchiveKeep, kDefaultArchiveKeep);
  registry->RegisterIntegerPref(kSleepAfterMinutes, kDefaultSleepAfterMinutes);
  registry->RegisterStringPref(kPinnedClose, kDefaultPinnedClose);
  registry->RegisterBooleanPref(kFolderRulesEnabled, true);
}

std::optional<base::TimeDelta> ParseArchiveAfter(std::string_view value) {
  if (value == "never") {
    return std::nullopt;
  }
  if (value == "24h") {
    return base::Hours(24);
  }
  if (value == "7d") {
    return base::Days(7);
  }
  // "12h", and anything unknown.
  return base::Hours(12);
}

std::optional<base::TimeDelta> ParseArchiveKeep(std::string_view value) {
  if (value == "forever") {
    return std::nullopt;
  }
  if (value == "7d") {
    return base::Days(7);
  }
  if (value == "90d") {
    return base::Days(90);
  }
  // "30d", and anything unknown.
  return base::Days(30);
}

std::optional<base::TimeDelta> SleepAfterFromMinutes(int minutes) {
  if (minutes == 0) {
    return std::nullopt;
  }
  if (minutes < 0) {
    minutes = kDefaultSleepAfterMinutes;
  }
  minutes = std::clamp(minutes, kMinSleepAfterMinutes, kMaxSleepAfterMinutes);
  return base::Minutes(minutes);
}

std::optional<base::TimeDelta> GetArchiveAfter(const PrefService& prefs) {
  return ParseArchiveAfter(prefs.GetString(kArchiveAfter));
}

std::optional<base::TimeDelta> GetArchiveKeep(const PrefService& prefs) {
  return ParseArchiveKeep(prefs.GetString(kArchiveKeep));
}

std::optional<base::TimeDelta> GetSleepAfter(const PrefService& prefs) {
  return SleepAfterFromMinutes(prefs.GetInteger(kSleepAfterMinutes));
}

bool AreFolderRulesEnabled(const PrefService& prefs) {
  return prefs.GetBoolean(kFolderRulesEnabled);
}

}  // namespace aerie_tab_model
