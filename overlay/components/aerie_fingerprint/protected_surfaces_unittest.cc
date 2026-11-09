// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_fingerprint/protected_surfaces.h"

#include <algorithm>
#include <set>

#include "testing/gtest/include/gtest/gtest.h"

namespace aerie_fingerprint {

TEST(ProtectedSurfacesTest, EverySurfaceIsListedOnce) {
  const std::vector<ProtectedSurface> surfaces = ProtectedSurfaces();
  const std::set<ProtectedSurface> unique(surfaces.begin(), surfaces.end());
  EXPECT_EQ(surfaces.size(), unique.size());
  // kScreen is the last value of the enum. A surface added after it must be
  // added to the list too.
  EXPECT_EQ(static_cast<size_t>(ProtectedSurface::kScreen) + 1,
            surfaces.size());
  for (ProtectedSurface surface :
       {ProtectedSurface::kCanvas, ProtectedSurface::kAudio,
        ProtectedSurface::kCpuCores, ProtectedSurface::kDeviceMemory,
        ProtectedSurface::kGraphicsCard, ProtectedSurface::kScreen}) {
    EXPECT_TRUE(std::ranges::contains(surfaces, surface));
  }
}

}  // namespace aerie_fingerprint
