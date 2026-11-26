// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_keyboard_layout/layout_map_policy.h"

namespace aerie_keyboard_layout {

bool ShouldRefuseLayoutMap(const Policy& policy) {
  return policy.site_protected;
}

}  // namespace aerie_keyboard_layout
