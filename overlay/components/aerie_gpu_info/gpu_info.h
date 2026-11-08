// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_GPU_INFO_GPU_INFO_H_
#define COMPONENTS_AERIE_GPU_INFO_GPU_INFO_H_

#include <string>
#include <string_view>

// What WebGL (WEBGL_debug_renderer_info) and WebGPU (adapter info) report about
// the graphics card (issue #112). Pure code: no Blink, no base, so it has unit
// tests. Blink only decides when to call it.
namespace aerie_gpu_info {

// The families Chrome reports on Linux. There is no "other": Chrome never
// reports one, so a string that named it would single out Aerie users.
enum class GpuFamily { kAmd, kIntel, kNvidia, kMesa };

// Maps the real GL_VENDOR and GL_RENDERER strings to a vendor family. Matching
// ignores case and looks for whole vendor words. Mesa's own driver version
// appears in the strings of AMD and Intel cards, so Mesa is the answer only for
// its software rasterizers (llvmpipe, softpipe, lavapipe) or when no card
// vendor is named.
GpuFamily GpuFamilyFromGlStrings(std::string_view vendor,
                                 std::string_view renderer);

// The strings reported in place of the real ones, in the format Chrome itself
// uses for UNMASKED_VENDOR_WEBGL and UNMASKED_RENDERER_WEBGL. They hold the
// vendor family and a fixed GL ES version, and no model, driver or LLVM
// version.
std::string MaskedVendor(GpuFamily family);
std::string MaskedRenderer(GpuFamily family);

enum class ContextKind { kWindow, kWorker };

// What decides whether a context reports masked GPU details.
struct MaskPolicy {
  // The HeliumNoise feature.
  bool master_enabled;
  // The HeliumNoiseWebGL feature.
  bool surface_enabled;
  // Whether the browser sent this window a kWebGL token (it sends none when
  // protection is off globally or for the site).
  bool has_token;
};

// Whether a context reports masked GPU details. A worker has no tokens yet
// (#113), so it masks whenever the features are on: fail closed.
bool ShouldMaskGpuInfo(ContextKind kind, const MaskPolicy& policy);

}  // namespace aerie_gpu_info

#endif  // COMPONENTS_AERIE_GPU_INFO_GPU_INFO_H_
