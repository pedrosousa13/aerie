// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_tab_model/sleep_summary.h"

namespace aerie_tab_model {

int RoundMemoryEstimateMb(int64_t bytes) {
  constexpr int64_t kMb = 1024 * 1024;
  if (bytes <= 0) {
    return 0;
  }
  // Half up, to the nearest 10 MB.
  return static_cast<int>((bytes + 5 * kMb) / (10 * kMb) * 10);
}

}  // namespace aerie_tab_model
