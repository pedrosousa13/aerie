// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_focus_ring.h"

#include <algorithm>
#include <memory>

#include "cc/paint/paint_flags.h"
#include "components/aerie_design_tokens/aerie_tokens.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/view_utils.h"

AerieSidebarFocusRing::AerieSidebarFocusRing(views::View* host,
                                             float corner_radius)
    : host_(host),
      corner_radius_(corner_radius),
      // Until the sidebar gives the Workspace accent, an opaque stroke of the
      // default theme's text colour; the sidebar sets the real one at once.
      color_(aerie::tokens::kColorTextLight) {
  SetCanProcessEventsWithinSubtree(false);
  GetViewAccessibility().SetIsIgnored(true);
  SetVisible(false);
  observation_.Observe(host);
}

AerieSidebarFocusRing::~AerieSidebarFocusRing() = default;

// static
AerieSidebarFocusRing* AerieSidebarFocusRing::Install(views::View* host,
                                                      float corner_radius) {
  auto* ring = host->AddChildView(std::unique_ptr<AerieSidebarFocusRing>(
      new AerieSidebarFocusRing(host, corner_radius)));
  ring->Refresh();
  return ring;
}

// static
AerieSidebarFocusRing* AerieSidebarFocusRing::Get(views::View* host) {
  for (views::View* child : host->children()) {
    if (auto* ring = views::AsViewClass<AerieSidebarFocusRing>(child)) {
      return ring;
    }
  }
  return nullptr;
}

void AerieSidebarFocusRing::SetColor(SkColor color) {
  if (color_ == color) {
    return;
  }
  color_ = color;
  SchedulePaint();
}

float AerieSidebarFocusRing::thickness() const {
  return aerie::tokens::kSizeFocusRingWidth;
}

float AerieSidebarFocusRing::offset() const {
  return aerie::tokens::kSizeFocusRingOffset;
}

void AerieSidebarFocusRing::Refresh() {
  // The ring covers the host and the room around it for the gap and the stroke.
  const int room = static_cast<int>(offset() + thickness());
  gfx::Rect bounds = host_->GetLocalBounds();
  bounds.Outset(room);
  SetBoundsRect(bounds);
  SetVisible(host_->HasFocus());
}

void AerieSidebarFocusRing::OnPaint(gfx::Canvas* canvas) {
  // The stroke's middle is `offset + thickness / 2` away from the host.
  const float middle = offset() + thickness() / 2.0f;
  gfx::RectF rect(GetLocalBounds());
  rect.Inset(gfx::InsetsF(thickness() / 2.0f));
  const float radius =
      corner_radius_ < 0
          ? std::min(rect.width(), rect.height()) / 2.0f
          : corner_radius_ + middle;
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setStyle(cc::PaintFlags::kStroke_Style);
  flags.setStrokeWidth(thickness());
  flags.setColor(color_);
  canvas->DrawRoundRect(rect, radius, flags);
}

void AerieSidebarFocusRing::OnViewFocused(views::View* observed_view) {
  Refresh();
}

void AerieSidebarFocusRing::OnViewBlurred(views::View* observed_view) {
  Refresh();
}

void AerieSidebarFocusRing::OnViewBoundsChanged(views::View* observed_view) {
  Refresh();
}

BEGIN_METADATA(AerieSidebarFocusRing)
END_METADATA
