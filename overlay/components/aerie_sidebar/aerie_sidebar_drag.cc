// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_sidebar/aerie_sidebar_drag.h"

#include <cstdlib>

namespace aerie_sidebar {

bool ExceedsDragThreshold(int dx, int dy) {
  return std::abs(dx) > kDragThreshold || std::abs(dy) > kDragThreshold;
}

}  // namespace aerie_sidebar
