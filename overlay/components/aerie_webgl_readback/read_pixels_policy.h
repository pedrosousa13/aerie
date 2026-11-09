// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_WEBGL_READBACK_READ_PIXELS_POLICY_H_
#define COMPONENTS_AERIE_WEBGL_READBACK_READ_PIXELS_POLICY_H_

#include <cstddef>
#include <cstdint>
#include <optional>

namespace aerie_webgl_readback {

// Which WebGL readPixels calls get fingerprinting noise (issue #119): only
// reads from the default framebuffer in RGBA and UNSIGNED_BYTE. A read from a
// framebuffer object or in any other format stays exact, because GPGPU, TF.js
// and video readback need exact values. |format| and |type| are GL enums.
bool ShouldNoiseReadPixels(bool default_framebuffer,
                           uint32_t format,
                           uint32_t type);

// Where the pixels of an RGBA/UNSIGNED_BYTE readPixels call sit in the
// destination buffer, given the pack parameters. The same arithmetic as
// WebGLImageConversion::ComputeImageSizeInBytes for four-byte pixels.
struct RgbaLayout {
  // Byte offset of the first pixel (PACK_SKIP_ROWS and PACK_SKIP_PIXELS).
  size_t first_pixel_offset;
  // Bytes from the start of one row to the start of the next (PACK_ROW_LENGTH
  // and PACK_ALIGNMENT).
  size_t row_stride;
  // Bytes the call writes, counted from the start of the buffer. The last row
  // has no padding.
  size_t total_bytes;
};

// nullopt for an empty or negative size, an alignment that is not positive, or
// a size that overflows.
std::optional<RgbaLayout> ComputeRgbaLayout(int width,
                                            int height,
                                            int pack_alignment,
                                            int pack_row_length,
                                            int pack_skip_pixels,
                                            int pack_skip_rows);

}  // namespace aerie_webgl_readback

#endif  // COMPONENTS_AERIE_WEBGL_READBACK_READ_PIXELS_POLICY_H_
