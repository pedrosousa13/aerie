// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_FINGERPRINT_PROTECTED_SURFACES_H_
#define COMPONENTS_AERIE_FINGERPRINT_PROTECTED_SURFACES_H_

#include <vector>

namespace aerie_fingerprint {

// What the fingerprinting protection changes on a page, one value per thing a
// script can read. The Guard panel lists these (read-only) under its
// fingerprinting row, and the list must say only what the code does: add a
// value here in the same change that adds the protection, and remove it when a
// protection goes. docs/hardening.md describes each one.
enum class ProtectedSurface {
  // Canvas pixel readback (Helium's canvas noise).
  kCanvas,
  // AudioBuffer and AnalyserNode output (stable per site and session).
  kAudio,
  // navigator.hardwareConcurrency.
  kCpuCores,
  // navigator.deviceMemory and its two request headers, capped at 8 GB.
  kDeviceMemory,
  // WebGL's unmasked vendor and renderer, and WebGPU adapter details.
  kGraphicsCard,
  // The screen size and the window position.
  kScreen,
  // The widths and heights CanvasRenderingContext2D.measureText returns.
  kTextMetrics,
};

// Every surface the protection covers, in the order the panel shows them. The
// list does not depend on the state of the switch: it says what the switch
// controls.
std::vector<ProtectedSurface> ProtectedSurfaces();

}  // namespace aerie_fingerprint

#endif  // COMPONENTS_AERIE_FINGERPRINT_PROTECTED_SURFACES_H_
