// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_navigator_pins/navigator_pins.h"

namespace aerie_navigator_pins {

bool ShouldPinNavigator(const Policy& policy) {
  return policy.master_enabled && policy.surface_enabled && policy.has_token;
}

std::optional<size_t> VoiceIndexToKeep(size_t voice_count,
                                       std::optional<size_t> first_default) {
  if (voice_count == 0) {
    return std::nullopt;
  }
  if (first_default.has_value() && *first_default < voice_count) {
    return first_default;
  }
  return 0;
}

}  // namespace aerie_navigator_pins
