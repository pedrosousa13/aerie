// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_NAVIGATOR_PINS_NAVIGATOR_PINS_H_
#define COMPONENTS_AERIE_NAVIGATOR_PINS_NAVIGATOR_PINS_H_

#include <cstddef>
#include <optional>

// The values that navigator.getBattery() and speechSynthesis.getVoices() report
// while fingerprinting protection is on (issue #120). Pure code: no Blink, no
// base, so it has unit tests. Blink only decides when to call it
// (patches/hardening/0021 and 0022).
namespace aerie_navigator_pins {

// What decides whether a window pins these values instead of reporting the
// real ones.
struct Policy {
  // The HeliumNoise feature.
  bool master_enabled;
  // The HeliumNoiseNavigator feature.
  bool surface_enabled;
  // Whether the browser sent this window a kNavigator token (it sends none
  // when protection is off globally or for the site).
  bool has_token;
};

// A window without a token reports the real values, so that turning protection
// off, globally or for the site, restores them.
bool ShouldPinNavigator(const Policy& policy);

// The battery a protected window reports: charging, full, and no time left to
// charge. A machine with no battery reports the same, so this is the value of
// a desktop. dischargingTime is Infinity (BatteryStatus's own default).
inline constexpr bool kBatteryCharging = true;
inline constexpr double kBatteryLevel = 1.0;
inline constexpr double kBatteryChargingTimeSeconds = 0.0;

// The one voice a protected window lists: the first voice the system marks as
// its default, or else the first voice. `first_default` is the index of the
// first voice with isDefault set, if there is one. An empty list stays empty.
std::optional<size_t> VoiceIndexToKeep(size_t voice_count,
                                       std::optional<size_t> first_default);

}  // namespace aerie_navigator_pins

#endif  // COMPONENTS_AERIE_NAVIGATOR_PINS_NAVIGATOR_PINS_H_
