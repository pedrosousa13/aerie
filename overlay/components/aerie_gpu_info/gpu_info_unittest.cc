// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_gpu_info/gpu_info.h"

#include <cctype>
#include <string>

#include "testing/gtest/include/gtest/gtest.h"

namespace aerie_gpu_info {
namespace {

// What Chrome's ANGLE layer reports through WEBGL_debug_renderer_info on
// Linux, for the families the issue names.
constexpr char kAmdVendor[] = "Google Inc. (AMD)";
constexpr char kAmdRenderer[] =
    "ANGLE (AMD, AMD Radeon Graphics (radeonsi, renoir, LLVM 15.0.7, DRM "
    "3.49, 6.1.0), OpenGL 4.6 (Core Profile) Mesa 23.0.4)";
constexpr char kIntelVendor[] = "Google Inc. (Intel)";
constexpr char kIntelRenderer[] =
    "ANGLE (Intel, Mesa Intel(R) UHD Graphics 620 (KBL GT2), OpenGL 4.6 "
    "(Core Profile) Mesa 23.0.4)";
constexpr char kNvidiaVendor[] = "Google Inc. (NVIDIA Corporation)";
constexpr char kNvidiaRenderer[] =
    "ANGLE (NVIDIA Corporation, NVIDIA GeForce GTX 1650/PCIe/SSE2, OpenGL "
    "4.5.0 NVIDIA 535.54.03)";
constexpr char kMesaVendor[] = "Google Inc. (Mesa)";
constexpr char kMesaRenderer[] =
    "ANGLE (Mesa, llvmpipe (LLVM 15.0.7, 256 bits), OpenGL 4.5 (Core Profile) "
    "Mesa 23.0.4)";

TEST(GpuFamilyTest, AmdIsNotMistakenForMesa) {
  // The AMD driver's version string ends in "Mesa 23.0.4".
  EXPECT_EQ(GpuFamilyFromGlStrings(kAmdVendor, kAmdRenderer), GpuFamily::kAmd);
}

TEST(GpuFamilyTest, IntelIsNotMistakenForMesa) {
  EXPECT_EQ(GpuFamilyFromGlStrings(kIntelVendor, kIntelRenderer),
            GpuFamily::kIntel);
}

TEST(GpuFamilyTest, Nvidia) {
  EXPECT_EQ(GpuFamilyFromGlStrings(kNvidiaVendor, kNvidiaRenderer),
            GpuFamily::kNvidia);
}

TEST(GpuFamilyTest, LlvmpipeIsMesa) {
  EXPECT_EQ(GpuFamilyFromGlStrings(kMesaVendor, kMesaRenderer),
            GpuFamily::kMesa);
}

TEST(GpuFamilyTest, OtherSoftwareRastersOfMesaAreMesa) {
  EXPECT_EQ(GpuFamilyFromGlStrings("Mesa", "softpipe"), GpuFamily::kMesa);
  EXPECT_EQ(GpuFamilyFromGlStrings("Mesa", "lavapipe"), GpuFamily::kMesa);
}

TEST(GpuFamilyTest, UnknownVendorIsOther) {
  EXPECT_EQ(GpuFamilyFromGlStrings("Google Inc. (Google)",
                                   "ANGLE (Google, Vulkan 1.3.0 (SwiftShader "
                                   "Device (Subzero)), SwiftShader driver)"),
            GpuFamily::kOther);
  EXPECT_EQ(GpuFamilyFromGlStrings("Apple Inc.", "Apple GPU"),
            GpuFamily::kOther);
  EXPECT_EQ(GpuFamilyFromGlStrings("", ""), GpuFamily::kOther);
}

TEST(GpuFamilyTest, MatchIgnoresCase) {
  EXPECT_EQ(GpuFamilyFromGlStrings("ati technologies inc.", "radeon hd 5450"),
            GpuFamily::kAmd);
  EXPECT_EQ(GpuFamilyFromGlStrings("nvidia corporation", "geforce 8800"),
            GpuFamily::kNvidia);
}

TEST(GpuFamilyTest, WordsThatMerelyContainAVendorNameAreOther) {
  // "ati" is inside "Corporation" and "intel" inside "intelligent".
  EXPECT_EQ(GpuFamilyFromGlStrings("Some Corporation", "intelligent renderer"),
            GpuFamily::kOther);
}

TEST(MaskedStringsTest, ChromeFormatPerFamily) {
  EXPECT_EQ(MaskedVendor(GpuFamily::kAmd), "Google Inc. (AMD)");
  EXPECT_EQ(MaskedRenderer(GpuFamily::kAmd),
            "ANGLE (AMD, AMD Radeon Graphics, OpenGL ES 3.2)");
  EXPECT_EQ(MaskedVendor(GpuFamily::kIntel), "Google Inc. (Intel)");
  EXPECT_EQ(MaskedRenderer(GpuFamily::kIntel),
            "ANGLE (Intel, Intel(R) Graphics, OpenGL ES 3.2)");
  EXPECT_EQ(MaskedVendor(GpuFamily::kNvidia), "Google Inc. (NVIDIA)");
  EXPECT_EQ(MaskedRenderer(GpuFamily::kNvidia),
            "ANGLE (NVIDIA, NVIDIA GeForce Graphics, OpenGL ES 3.2)");
  EXPECT_EQ(MaskedVendor(GpuFamily::kMesa), "Google Inc. (Mesa)");
  EXPECT_EQ(MaskedRenderer(GpuFamily::kMesa),
            "ANGLE (Mesa, Mesa llvmpipe, OpenGL ES 3.2)");
  EXPECT_EQ(MaskedVendor(GpuFamily::kOther), "Google Inc. (Other)");
  EXPECT_EQ(MaskedRenderer(GpuFamily::kOther),
            "ANGLE (Other, Generic Graphics, OpenGL ES 3.2)");
}

TEST(MaskedStringsTest, NoModelDriverOrBuildNumberRemains) {
  // The only digits allowed are the GL ES version, "3.2".
  for (GpuFamily family : {GpuFamily::kAmd, GpuFamily::kIntel,
                           GpuFamily::kNvidia, GpuFamily::kMesa,
                           GpuFamily::kOther}) {
    std::string text = MaskedVendor(family) + MaskedRenderer(family);
    std::string without_version = text;
    size_t at = without_version.find("OpenGL ES 3.2");
    ASSERT_NE(at, std::string::npos);
    without_version.erase(at);
    for (char c : without_version) {
      EXPECT_FALSE(std::isdigit(static_cast<unsigned char>(c))) << text;
    }
    EXPECT_EQ(text.find("Aerie"), std::string::npos);
    EXPECT_FALSE(MaskedVendor(family).empty());
    EXPECT_FALSE(MaskedRenderer(family).empty());
  }
}

TEST(ShouldMaskTest, WindowFollowsItsToken) {
  EXPECT_TRUE(ShouldMaskGpuInfo(ContextKind::kWindow, true, true, true));
  EXPECT_FALSE(ShouldMaskGpuInfo(ContextKind::kWindow, true, true, false));
}

TEST(ShouldMaskTest, WorkerMasksWithoutTokens) {
  // Fail closed until workers get tokens (#113).
  EXPECT_TRUE(ShouldMaskGpuInfo(ContextKind::kWorker, true, true, false));
  EXPECT_TRUE(ShouldMaskGpuInfo(ContextKind::kWorker, true, true, true));
}

TEST(ShouldMaskTest, TheFeatureSwitchesRestoreRealValues) {
  for (ContextKind kind : {ContextKind::kWindow, ContextKind::kWorker}) {
    EXPECT_FALSE(ShouldMaskGpuInfo(kind, true, false, true));
    EXPECT_FALSE(ShouldMaskGpuInfo(kind, false, true, true));
  }
}

}  // namespace
}  // namespace aerie_gpu_info
