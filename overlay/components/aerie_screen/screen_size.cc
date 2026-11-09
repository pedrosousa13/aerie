// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_screen/screen_size.h"

#include <algorithm>
#include <iterator>

namespace aerie_screen {
namespace {

// Ascending in both dimensions. The Bench check (hardening_fingerprinting_screen)
// keeps a copy of this list; a unit test keeps the two equal.
constexpr Size kBuckets[] = {
    {1280, 800},  {1366, 768},  {1440, 900},  {1680, 1050},
    {1920, 1080}, {2560, 1440}, {3840, 2160},
};

}  // namespace

Size ScreenSizeForWindow(int outer_width, int outer_height) {
  const int width = std::max(outer_width, 0);
  const int height = std::max(outer_height, 0);
  for (const Size& bucket : kBuckets) {
    if (bucket.width >= width && bucket.height >= height) {
      return bucket;
    }
  }
  const Size& largest = kBuckets[std::size(kBuckets) - 1];
  return {std::max(largest.width, width), std::max(largest.height, height)};
}

bool ShouldProtectScreen(const Policy& policy) {
  return policy.master_enabled && policy.surface_enabled && policy.has_token;
}

}  // namespace aerie_screen
