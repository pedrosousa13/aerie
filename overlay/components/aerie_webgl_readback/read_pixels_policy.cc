// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_webgl_readback/read_pixels_policy.h"

#include <limits>

namespace aerie_webgl_readback {

namespace {

constexpr uint32_t kGlRgba = 0x1908;
constexpr uint32_t kGlUnsignedByte = 0x1401;
constexpr size_t kBytesPerPixel = 4;

// a * b, or false on overflow of size_t.
bool Multiply(size_t a, size_t b, size_t* out) {
  if (a != 0 && b > std::numeric_limits<size_t>::max() / a) {
    return false;
  }
  *out = a * b;
  return true;
}

bool Add(size_t a, size_t b, size_t* out) {
  if (b > std::numeric_limits<size_t>::max() - a) {
    return false;
  }
  *out = a + b;
  return true;
}

}  // namespace

bool ShouldNoiseReadPixels(bool default_framebuffer,
                           uint32_t format,
                           uint32_t type) {
  return default_framebuffer && format == kGlRgba && type == kGlUnsignedByte;
}

std::optional<RgbaLayout> ComputeRgbaLayout(int width,
                                            int height,
                                            int pack_alignment,
                                            int pack_row_length,
                                            int pack_skip_pixels,
                                            int pack_skip_rows) {
  if (width <= 0 || height <= 0 || pack_alignment <= 0 ||
      pack_row_length < 0 || pack_skip_pixels < 0 || pack_skip_rows < 0) {
    return std::nullopt;
  }
  const size_t row_pixels = static_cast<size_t>(
      pack_row_length > 0 ? pack_row_length : width);
  size_t row_bytes = 0;
  size_t stride = 0;
  size_t skip_rows_bytes = 0;
  size_t skip_pixels_bytes = 0;
  size_t last_row_start = 0;
  size_t first = 0;
  size_t total = 0;
  if (!Multiply(row_pixels, kBytesPerPixel, &row_bytes)) {
    return std::nullopt;
  }
  const size_t alignment = static_cast<size_t>(pack_alignment);
  const size_t residual = row_bytes % alignment;
  stride = row_bytes;
  if (residual != 0 && !Add(row_bytes, alignment - residual, &stride)) {
    return std::nullopt;
  }
  if (!Multiply(static_cast<size_t>(pack_skip_rows), stride,
                &skip_rows_bytes) ||
      !Multiply(static_cast<size_t>(pack_skip_pixels), kBytesPerPixel,
                &skip_pixels_bytes) ||
      !Add(skip_rows_bytes, skip_pixels_bytes, &first) ||
      !Multiply(static_cast<size_t>(height - 1), stride, &last_row_start) ||
      !Add(first, last_row_start, &total) ||
      !Add(total, static_cast<size_t>(width) * kBytesPerPixel, &total)) {
    return std::nullopt;
  }
  return RgbaLayout{first, stride, total};
}

}  // namespace aerie_webgl_readback
