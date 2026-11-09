// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_webgl_readback/read_pixels_policy.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace aerie_webgl_readback {
namespace {

constexpr uint32_t kRgba = 0x1908;
constexpr uint32_t kRgb = 0x1907;
constexpr uint32_t kRedInteger = 0x8D94;
constexpr uint32_t kUnsignedByte = 0x1401;
constexpr uint32_t kFloat = 0x1406;
constexpr uint32_t kHalfFloat = 0x140B;
constexpr uint32_t kUnsignedShort4444 = 0x8033;

TEST(ShouldNoiseReadPixelsTest, DefaultFramebufferRgbaUnsignedByteIsNoised) {
  EXPECT_TRUE(ShouldNoiseReadPixels(true, kRgba, kUnsignedByte));
}

TEST(ShouldNoiseReadPixelsTest, FramebufferObjectsStayExact) {
  EXPECT_FALSE(ShouldNoiseReadPixels(false, kRgba, kUnsignedByte));
}

TEST(ShouldNoiseReadPixelsTest, OtherFormatsAndTypesStayExact) {
  EXPECT_FALSE(ShouldNoiseReadPixels(true, kRgba, kFloat));
  EXPECT_FALSE(ShouldNoiseReadPixels(true, kRgba, kHalfFloat));
  EXPECT_FALSE(ShouldNoiseReadPixels(true, kRgba, kUnsignedShort4444));
  EXPECT_FALSE(ShouldNoiseReadPixels(true, kRgb, kUnsignedByte));
  EXPECT_FALSE(ShouldNoiseReadPixels(true, kRedInteger, kUnsignedByte));
}

TEST(RgbaLayoutTest, TightRowsStartAtZero) {
  auto layout = ComputeRgbaLayout(10, 3, 4, 0, 0, 0);
  ASSERT_TRUE(layout.has_value());
  EXPECT_EQ(layout->first_pixel_offset, 0u);
  EXPECT_EQ(layout->row_stride, 40u);
  EXPECT_EQ(layout->total_bytes, 120u);
}

TEST(RgbaLayoutTest, RowLengthWidensTheStrideAndTheLastRowIsNotPadded) {
  auto layout = ComputeRgbaLayout(10, 3, 4, 16, 0, 0);
  ASSERT_TRUE(layout.has_value());
  EXPECT_EQ(layout->row_stride, 64u);
  EXPECT_EQ(layout->total_bytes, 2u * 64u + 40u);
}

TEST(RgbaLayoutTest, SkipPixelsAndSkipRowsMoveTheFirstPixel) {
  auto layout = ComputeRgbaLayout(10, 3, 4, 16, 2, 5);
  ASSERT_TRUE(layout.has_value());
  EXPECT_EQ(layout->first_pixel_offset, 5u * 64u + 2u * 4u);
  EXPECT_EQ(layout->total_bytes, layout->first_pixel_offset + 2u * 64u + 40u);
}

TEST(RgbaLayoutTest, EightByteAlignmentPadsAnOddRowLength) {
  // 5 pixels are 20 bytes, and the next multiple of 8 is 24.
  auto layout = ComputeRgbaLayout(5, 2, 8, 0, 0, 0);
  ASSERT_TRUE(layout.has_value());
  EXPECT_EQ(layout->row_stride, 24u);
  EXPECT_EQ(layout->total_bytes, 24u + 20u);
}

TEST(RgbaLayoutTest, RejectsEmptyAndNegativeInputs) {
  EXPECT_FALSE(ComputeRgbaLayout(0, 3, 4, 0, 0, 0).has_value());
  EXPECT_FALSE(ComputeRgbaLayout(3, 0, 4, 0, 0, 0).has_value());
  EXPECT_FALSE(ComputeRgbaLayout(-1, 3, 4, 0, 0, 0).has_value());
  EXPECT_FALSE(ComputeRgbaLayout(3, 3, 4, 0, -1, 0).has_value());
  EXPECT_FALSE(ComputeRgbaLayout(3, 3, 0, 0, 0, 0).has_value());
}

TEST(RgbaLayoutTest, RejectsAnOverflow) {
  // On a 64-bit size_t a 2^31 by 2^31 area still fits, so the sum of the skip
  // rows and the last row start is what overflows.
  EXPECT_FALSE(ComputeRgbaLayout(0x7fffffff, 0x7fffffff, 4, 0, 0x7fffffff,
                                 0x7fffffff)
                   .has_value());
}

}  // namespace
}  // namespace aerie_webgl_readback
