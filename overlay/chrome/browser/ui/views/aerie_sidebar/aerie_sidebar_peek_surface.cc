// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_peek_surface.h"

#include <algorithm>
#include <vector>

#include "cc/paint/paint_flags.h"
#include "components/aerie_design_tokens/aerie_tokens.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect_conversions.h"
#include "ui/gfx/shadow_value.h"
#include "ui/gfx/skia_paint_util.h"
#include "ui/views/view_class_properties.h"

AerieSidebarPeekSurface::AerieSidebarPeekSurface() {
  SetProperty(views::kViewIgnoredByLayoutKey, true);
  SetCanProcessEventsWithinSubtree(false);
  SetPaintToLayer();
  layer()->SetFillsBoundsOpaquely(false);
}

AerieSidebarPeekSurface::~AerieSidebarPeekSurface() = default;

int AerieSidebarPeekSurface::ShadowRoom() {
  // Both themes' tokens have the same geometry; only the colours differ.
  const aerie::tokens::Shadow& token = aerie::tokens::kShadowPeekLight;
  int room = 0;
  for (int i = 0; i < token.count; ++i) {
    const aerie::tokens::ShadowLayer& layer = token.layers[i];
    room = std::max(room, layer.blur + std::abs(layer.y) + std::abs(layer.x) +
                              layer.spread);
  }
  return room;
}

void AerieSidebarPeekSurface::SetColours(
    const aerie_sidebar::SidebarPalette& palette) {
  palette_ = palette;
  SchedulePaint();
}

void AerieSidebarPeekSurface::OnPaint(gfx::Canvas* canvas) {
  gfx::Rect inner = GetLocalBounds();
  inner.Inset(ShadowRoom());
  const SkRect rect = gfx::RectToSkRect(inner);
  const float radius = aerie::tokens::kRadiusLg;
  std::vector<gfx::ShadowValue> shadows;
  const aerie::tokens::Shadow& token = palette_.peek_shadow;
  for (int i = 0; i < token.count; ++i) {
    const aerie::tokens::ShadowLayer& layer = token.layers[i];
    shadows.emplace_back(gfx::Vector2d(layer.x, layer.y), layer.blur,
                         layer.color);
  }
  cc::PaintFlags shadow_flags;
  shadow_flags.setAntiAlias(true);
  shadow_flags.setColor(SkColors::kTransparent);
  shadow_flags.setLooper(gfx::CreateShadowDrawLooper(shadows));
  canvas->sk_canvas()->drawRoundRect(rect, radius, radius, shadow_flags);
  cc::PaintFlags fill;
  fill.setAntiAlias(true);
  fill.setColor(palette_.peek_surface);
  canvas->DrawRoundRect(gfx::RectF(inner), radius, fill);
}

BEGIN_METADATA(AerieSidebarPeekSurface)
END_METADATA
