// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_frame/aerie_frame_corners.h"

#include "components/aerie_design_tokens/aerie_tokens.h"

namespace aerie_frame {

float WindowCornerRadius(float provider_radius,
                         bool restored,
                         bool can_draw_transparent_corners) {
  if (!restored || !can_draw_transparent_corners) {
    return 0;
  }
  return provider_radius > 0 ? provider_radius
                             : static_cast<float>(aerie::tokens::kRadiusXl);
}

}  // namespace aerie_frame
