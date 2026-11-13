// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_drag_overlay.h"

#include <vector>

#include "cc/paint/paint_flags.h"
#include "components/aerie_design_tokens/aerie_tokens.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/shadow_value.h"
#include "ui/gfx/skia_paint_util.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/layout_provider.h"
#include "ui/views/style/typography.h"
#include "ui/views/view_class_properties.h"

namespace {

SkColor DangerColor(bool dark) {
  return dark ? aerie::tokens::kColorStatusDangerDark
              : aerie::tokens::kColorStatusDangerLight;
}

}  // namespace

AerieSidebarDragGhost::AerieSidebarDragGhost(
    const AerieSidebarDragContent& content,
    const aerie_sidebar::SidebarPalette& palette,
    bool dark)
    : palette_(palette),
      dark_(dark),
      tile_(content.tile),
      indent_(content.indent) {
  // The layout of the sidebar must never place or size the drag layer.
  SetProperty(views::kViewIgnoredByLayoutKey, true);
  SetPaintToLayer();
  layer()->SetFillsBoundsOpaquely(false);
  SetCanProcessEventsWithinSubtree(false);
  icon_ = AddChildView(std::make_unique<views::ImageView>());
  icon_->SetImage(content.icon);
  if (!tile_) {
    title_ = AddChildView(std::make_unique<views::Label>(
        content.title, views::style::CONTEXT_LABEL, views::style::STYLE_BODY_4));
    title_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    title_->SetEnabledColor(palette_.text);
    title_->SetElideBehavior(gfx::ELIDE_TAIL);
    title_->SetBackgroundColor(SkColorSetA(palette_.text, 0));
  }
}

AerieSidebarDragGhost::~AerieSidebarDragGhost() = default;

void AerieSidebarDragGhost::SetColours(
    const aerie_sidebar::SidebarPalette& palette,
    bool dark) {
  palette_ = palette;
  dark_ = dark;
  if (title_) {
    title_->SetEnabledColor(palette_.text);
    title_->SetBackgroundColor(SkColorSetA(palette_.text, 0));
  }
  SchedulePaint();
}

void AerieSidebarDragGhost::SetContentBounds(const gfx::Rect& rect) {
  gfx::Rect outset = rect;
  outset.Outset(kShadowRoom);
  SetBoundsRect(outset);
}

gfx::Rect AerieSidebarDragGhost::content_bounds() const {
  gfx::Rect rect = bounds();
  rect.Inset(kShadowRoom);
  return rect;
}

void AerieSidebarDragGhost::Layout(PassKey) {
  const gfx::Rect inner(kShadowRoom, kShadowRoom,
                        width() - 2 * kShadowRoom, height() - 2 * kShadowRoom);
  const int icon = aerie::tokens::kSizeIcon;
  if (tile_) {
    icon_->SetBoundsRect(gfx::Rect(
        inner.x() + (inner.width() - icon) / 2,
        inner.y() + (inner.height() - icon) / 2, icon, icon));
    return;
  }
  const int x = inner.x() + indent_ + aerie::tokens::kSpace8;
  icon_->SetBoundsRect(
      gfx::Rect(x, inner.y() + (inner.height() - icon) / 2, icon, icon));
  const int text_x = x + icon + aerie::tokens::kSpace8;
  title_->SetBoundsRect(gfx::Rect(
      text_x, inner.y(), std::max(0, inner.right() - text_x - aerie::tokens::kSpace8),
      inner.height()));
}

void AerieSidebarDragGhost::OnPaint(gfx::Canvas* canvas) {
  const aerie::tokens::Shadow& token =
      dark_ ? aerie::tokens::kShadowLiftDark : aerie::tokens::kShadowLiftLight;
  gfx::Rect inner = GetLocalBounds();
  inner.Inset(kShadowRoom);
  const SkRect rect = gfx::RectToSkRect(inner);
  const float radius = aerie::tokens::kRadiusMd;
  // The shadow switches on with the drag; nothing animates it.
  std::vector<gfx::ShadowValue> shadows;
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
  fill.setColor(palette_.row_selected);
  canvas->DrawRoundRect(gfx::RectF(inner), radius, fill);
}

BEGIN_METADATA(AerieSidebarDragGhost)
END_METADATA

AerieSidebarDropIndicator::AerieSidebarDropIndicator(
    const aerie_sidebar::SidebarPalette& palette,
    bool dark)
    : palette_(palette), dark_(dark) {
  SetProperty(views::kViewIgnoredByLayoutKey, true);
  SetCanProcessEventsWithinSubtree(false);
  // Always visible, with empty bounds when there is nothing to show: a change
  // of visibility would lay the sidebar out again.
}

AerieSidebarDropIndicator::~AerieSidebarDropIndicator() = default;

void AerieSidebarDropIndicator::SetColours(
    const aerie_sidebar::SidebarPalette& palette,
    bool dark) {
  palette_ = palette;
  dark_ = dark;
  SchedulePaint();
}

void AerieSidebarDropIndicator::Show(Mode mode, const gfx::Rect& rect) {
  mode_ = mode;
  shown_ = true;
  SetBoundsRect(rect);
  SchedulePaint();
}

void AerieSidebarDropIndicator::Hide() {
  shown_ = false;
  SetBoundsRect(gfx::Rect());
}

void AerieSidebarDropIndicator::OnPaint(gfx::Canvas* canvas) {
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  const gfx::RectF local(GetLocalBounds());
  if (mode_ == Mode::kLine) {
    flags.setColor(palette_.accent);
    canvas->DrawRoundRect(local, 1.f, flags);
    return;
  }
  flags.setStyle(cc::PaintFlags::kStroke_Style);
  flags.setStrokeWidth(kDropMarkThickness);
  flags.setColor(mode_ == Mode::kRefused ? DangerColor(dark_) : palette_.accent);
  gfx::RectF inset = local;
  inset.Inset(kDropMarkThickness / 2.f);
  canvas->DrawRoundRect(inset, aerie::tokens::kRadiusMd, flags);
}

BEGIN_METADATA(AerieSidebarDropIndicator)
END_METADATA
