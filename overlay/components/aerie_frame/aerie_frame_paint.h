// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_FRAME_AERIE_FRAME_PAINT_H_
#define COMPONENTS_AERIE_FRAME_AERIE_FRAME_PAINT_H_

#include <optional>
#include <string>

#include "components/aerie_frame/aerie_frame_corners.h"
#include "components/aerie_frame/aerie_frame_look.h"
#include "components/aerie_frame/aerie_frame_palette.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/gfx/geometry/size.h"

namespace gfx {
class Canvas;
}

namespace aerie_frame {

// Paints the frame into `canvas`, which is `size` big: the gradient, the shade,
// the glow, and the page card's shadow around `card` (the card's own pixels are
// the page's). No image is decoded and no texture is uploaded: it is three
// gradient fills and two blurred rounded rectangles.
//
// `window_corners` are the radii of the window's own corners (issue #249): the
// OS draws the window with rounded top corners under client-side decorations,
// and the frame view leaves them transparent, so nothing is painted outside
// them. Square (the default) paints the whole box.
void PaintFrame(gfx::Canvas* canvas,
                const FramePalette& palette,
                const gfx::Size& size,
                bool sidebar_on_right,
                const gfx::Rect& card,
                const gfx::RoundedCornersF& window_corners =
                    gfx::RoundedCornersF());

// What the paint depends on. The tint view paints when this changes, and only
// then (issue #178: "paint once and on state change only, never per frame").
struct FrameTintInputs {
  aerie::tokens::ThemeId theme = kDefaultTheme;
  aerie::tokens::ColorMode mode = aerie::tokens::ColorMode::kLight;
  std::string colour;
  gfx::Size size;
  gfx::Rect card;
  bool sidebar_on_right = false;
  // The radii of the window's own corners (issue #249). All zero: square.
  gfx::RoundedCornersF window_corners;
  // Forced colours: the operating system's colours the frame is drawn in.
  // Empty: the theme's own.
  std::optional<SystemColours> system_colours;

  bool operator==(const FrameTintInputs&) const = default;
};

// The palette the inputs ask for: the theme's, or the system colours' under
// forced colours.
FramePalette PaletteOf(const FrameTintInputs& inputs);

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
