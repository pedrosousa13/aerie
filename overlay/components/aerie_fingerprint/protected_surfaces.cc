// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_fingerprint/protected_surfaces.h"

namespace aerie_fingerprint {

std::vector<ProtectedSurface> ProtectedSurfaces() {
  return {
      ProtectedSurface::kCanvas,        ProtectedSurface::kAudio,
      ProtectedSurface::kCpuCores,      ProtectedSurface::kDeviceMemory,
      ProtectedSurface::kGraphicsCard,  ProtectedSurface::kScreen,
  };
}

}  // namespace aerie_fingerprint
