// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_frame/aerie_frame_paint.h"

#include <cmath>
#include <numbers>
#include <vector>

#include "cc/paint/paint_flags.h"
#include "cc/paint/paint_shader.h"
#include "third_party/skia/include/core/SkMatrix.h"
#include "third_party/skia/include/effects/SkGradient.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/vector2d.h"
#include "ui/gfx/shadow_value.h"
#include "ui/gfx/skia_paint_util.h"

namespace aerie_frame {
namespace {

void PaintGradient(gfx::Canvas* canvas,
                   const FramePalette& palette,
                   const gfx::Size& size) {
  const double w = size.width();
  const double h = size.height();
  const double angle = palette.angle_degrees * std::numbers::pi / 180.0;
  const double length = std::abs(w * std::sin(angle)) + std::abs(h * std::cos(angle));
  const double dx = std::sin(angle) * length / 2;
  const double dy = -std::cos(angle) * length / 2;
  const SkPoint points[2] = {SkPoint::Make(w / 2 - dx, h / 2 - dy),
                             SkPoint::Make(w / 2 + dx, h / 2 + dy)};

  std::vector<SkColor4f> colors;
  std::vector<SkScalar> positions;
  const int count = std::max(palette.stop_count, 2);
  for (int i = 0; i < count; ++i) {
    colors.push_back(SkColor4f::FromColor(
        palette.stops[std::min(i, palette.stop_count - 1)]));
    positions.push_back(static_cast<SkScalar>(i) / (count - 1));
  }
  cc::PaintFlags flags;
  flags.setStyle(cc::PaintFlags::kFill_Style);
  flags.setShader(cc::PaintShader::MakeLinearGradient(
      points, colors.data(), positions.data(), count, SkTileMode::kClamp));
  canvas->DrawRect(gfx::RectF(size.width(), size.height()), flags);
}

void PaintShade(gfx::Canvas* canvas, const FramePalette& palette,
                const gfx::Size& size) {
  if (palette.shade <= 0) {
    return;
  }
  cc::PaintFlags flags;
  flags.setStyle(cc::PaintFlags::kFill_Style);
  flags.setColor(SkColor4f{0, 0, 0, palette.shade});
  canvas->DrawRect(gfx::RectF(size.width(), size.height()), flags);
}

// White light at the top left corner: an ellipse of 72% by 48% of the box
// (the 120% by 80% radial gradient, cut off at 60%), full strength at the
// corner and none at its edge.
void PaintGlow(gfx::Canvas* canvas, const FramePalette& palette,
               const gfx::Size& size) {
  if (palette.glow <= 0) {
    return;
  }
  const SkColor4f colors[2] = {SkColor4f{1, 1, 1, palette.glow},
                               SkColor4f{1, 1, 1, 0}};
  const SkScalar positions[2] = {0, 1};
  const SkMatrix to_ellipse =
      SkMatrix::Scale(0.72f * size.width(), 0.48f * size.height());
  cc::PaintFlags flags;
  flags.setStyle(cc::PaintFlags::kFill_Style);
  flags.setShader(cc::PaintShader::MakeRadialGradient(
      SkPoint::Make(0, 0), 1, colors, positions, 2, SkTileMode::kClamp,
      SkGradient::Interpolation(), 0, &to_ellipse));
  canvas->DrawRect(gfx::RectF(size.width(), size.height()), flags);
}

// The card's shadow, from the token layers (the first layer is the top one, so
// they are painted last to first), then the card's own surface colour under the
// page, so a page that has not painted yet shows the token colour.
void PaintCard(gfx::Canvas* canvas, const FramePalette& palette,
               const gfx::Rect& card) {
  if (card.IsEmpty()) {
    return;
  }
  const aerie::tokens::Shadow& shadow = palette.card_shadow;
  for (int i = shadow.count - 1; i >= 0; --i) {
    const aerie::tokens::ShadowLayer& layer = shadow.layers[i];
    gfx::Rect rect = card;
    rect.Outset(layer.spread);
    const int radius = palette.card_radius + layer.spread;
    cc::PaintFlags flags;
    flags.setStyle(cc::PaintFlags::kFill_Style);
    flags.setAntiAlias(true);
    if (layer.blur <= 0) {
      rect.Offset(layer.x, layer.y);
      flags.setColor(layer.color);
    } else {
      // The looper draws the shadow, offset and blurred, and then the shape
      // itself in the card colour, which the page covers.
      flags.setColor(palette.card_surface);
      flags.setLooper(gfx::CreateShadowDrawLooper(
          {gfx::ShadowValue(gfx::Vector2d(layer.x, layer.y), layer.blur,
                            layer.color)}));
    }
    canvas->DrawRoundRect(rect, radius, flags);
  }
  cc::PaintFlags surface;
  surface.setStyle(cc::PaintFlags::kFill_Style);
  surface.setAntiAlias(true);
  surface.setColor(palette.card_surface);
  canvas->DrawRoundRect(card, palette.card_radius, surface);
}

}  // namespace

void PaintFrame(gfx::Canvas* canvas,
                const FramePalette& palette,
                const gfx::Size& size,
                bool sidebar_on_right,
                const gfx::Rect& card) {
  canvas->Save();
  if (sidebar_on_right) {
    canvas->Translate(gfx::Vector2d(size.width(), 0));
    canvas->Scale(-1, 1);
  }
  PaintGradient(canvas, palette, size);
  PaintShade(canvas, palette, size);
  PaintGlow(canvas, palette, size);
  canvas->Restore();
  PaintCard(canvas, palette, card);
}

bool FrameTintState::Update(const FrameTintInputs& inputs) {
  if (has_inputs_ && inputs == inputs_) {
    return false;
  }
  inputs_ = inputs;
  has_inputs_ = true;
  return true;
}

}  // namespace aerie_frame
