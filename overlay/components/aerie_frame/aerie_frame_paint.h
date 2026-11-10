// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_FRAME_AERIE_FRAME_PAINT_H_
#define COMPONENTS_AERIE_FRAME_AERIE_FRAME_PAINT_H_

#include <string>

#include "components/aerie_frame/aerie_frame_palette.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"

namespace gfx {
class Canvas;
}

namespace aerie_frame {

// Paints the frame into `canvas`, which is `size` big: the gradient, the shade,
// the glow, and the page card's shadow around `card` (the card's own pixels are
// the page's). No image is decoded and no texture is uploaded: it is three
// gradient fills and two blurred rounded rectangles.
void PaintFrame(gfx::Canvas* canvas,
                const FramePalette& palette,
                const gfx::Size& size,
                bool sidebar_on_right,
                const gfx::Rect& card);

// What the paint depends on. The tint view paints when this changes, and only
// then (issue #178: "paint once and on state change only, never per frame").
struct FrameTintInputs {
  aerie::tokens::ThemeId theme = kDefaultTheme;
  aerie::tokens::ColorMode mode = aerie::tokens::ColorMode::kLight;
  std::string colour;
  gfx::Size size;
  gfx::Rect card;
  bool sidebar_on_right = false;

  bool operator==(const FrameTintInputs&) const = default;
};

// Decides when the frame needs a paint and counts the paints.
class FrameTintState {
 public:
  // Returns true when `inputs` differ from the ones the last Update() saw (the
  // first call always does). The caller schedules one paint for a true result.
  bool Update(const FrameTintInputs& inputs);
  const FrameTintInputs& inputs() const { return inputs_; }

  // Called from OnPaint.
  void RecordPaint() { ++paint_count_; }
  int paint_count() const { return paint_count_; }

 private:
  FrameTintInputs inputs_;
  bool has_inputs_ = false;
  int paint_count_ = 0;
};

}  // namespace aerie_frame

#endif  // COMPONENTS_AERIE_FRAME_AERIE_FRAME_PAINT_H_
