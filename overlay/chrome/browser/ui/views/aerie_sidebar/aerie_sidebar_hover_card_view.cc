// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_hover_card_view.h"

#include <algorithm>

#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_peek_surface.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_tab_snapshot.h"
#include "components/aerie_design_tokens/aerie_tokens.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/canvas.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/style/typography.h"

namespace {

constexpr int kCardWidth = aerie::tokens::kSizeHoverCardWidth;
constexpr int kSnapshotHeight = aerie::tokens::kSizeHoverCardSnapshotHeight;
// Room between the edge of the card and its parts.
constexpr int kPadding = aerie::tokens::kSpace12;
// The gap under the snapshot, and between the favicon and the title.
constexpr int kGap = aerie::tokens::kSpace8;
// The URL may take this many lines before it is cut with an ellipsis.
constexpr int kUrlMaxLines = 3;

}  // namespace

AerieHoverCardBody::AerieHoverCardBody(
    const aerie_sidebar::SidebarPalette& palette)
    : palette_(palette) {
  favicon_ = AddChildView(std::make_unique<views::ImageView>());
  favicon_->SetCanProcessEventsWithinSubtree(false);
  title_ = AddChildView(std::make_unique<views::Label>(
      std::u16string(), views::style::CONTEXT_LABEL, views::style::STYLE_BODY_4));
  title_->SetHorizontalAlignment(gfx::ALIGN_TO_HEAD);
  title_->SetElideBehavior(gfx::ELIDE_TAIL);
  title_->SetEnabledColor(palette_.text);
  host_ = AddChildView(std::make_unique<views::Label>(
      std::u16string(), views::style::CONTEXT_LABEL, views::style::STYLE_BODY_5));
  host_->SetHorizontalAlignment(gfx::ALIGN_TO_HEAD);
  host_->SetElideBehavior(gfx::ELIDE_TAIL);
  host_->SetEnabledColor(palette_.text_muted);
  url_ = AddChildView(std::make_unique<views::Label>(
      std::u16string(), views::style::CONTEXT_LABEL, views::style::STYLE_BODY_5));
  url_->SetHorizontalAlignment(gfx::ALIGN_TO_HEAD);
  url_->SetMultiLine(true);
  url_->SetMaxLines(kUrlMaxLines);
  url_->SetElideBehavior(gfx::ELIDE_TAIL);
  url_->SetEnabledColor(palette_.text_muted);
  // The card is read through the row it describes (its accessible description),
  // so the card itself is not a stop for a screen reader.
  GetViewAccessibility().SetIsIgnored(true);
}

AerieHoverCardBody::~AerieHoverCardBody() = default;

void AerieHoverCardBody::SetText(const aerie_sidebar::HoverCardText& text,
                                 const ui::ImageModel& favicon) {
  title_->SetText(text.title);
  host_->SetText(text.host);
  url_->SetText(text.url);
  url_->SetVisible(!text.url.empty());
  favicon_->SetImage(favicon);
  InvalidateLayout();
  SchedulePaint();
}

void AerieHoverCardBody::SetSnapshot(const gfx::ImageSkia& snapshot) {
  const bool changed_area = snapshot.isNull() != snapshot_.isNull();
  snapshot_ = snapshot;
  if (changed_area) {
    // The card grows or shrinks by the area: its window follows.
    InvalidateLayout();
    PreferredSizeChanged();
  }
  SchedulePaint();
}

int AerieHoverCardBody::ContentWidth() const {
  return kCardWidth - 2 * kPadding;
}

gfx::Rect AerieHoverCardBody::SnapshotBounds() const {
  if (snapshot_.isNull()) {
    return gfx::Rect();
  }
  return gfx::Rect(kPadding, kPadding, ContentWidth(), kSnapshotHeight);
}

gfx::Size AerieHoverCardBody::CalculatePreferredSize(
    const views::SizeBounds&) const {
  int height = kPadding;
  if (!snapshot_.isNull()) {
    height += kSnapshotHeight + kGap;
  }
  height += title_->GetPreferredSize().height() +
            host_->GetPreferredSize().height();
  if (url_->GetVisible()) {
    height += url_->GetHeightForWidth(ContentWidth());
  }
  return gfx::Size(kCardWidth, height + kPadding);
}

void AerieHoverCardBody::Layout(PassKey) {
  // Every part is placed as for left-to-right and then mirrored, so that a
  // right-to-left UI (which also mirrors the sidebar) reads from the right.
  int y = kPadding;
  if (!snapshot_.isNull()) {
    y += kSnapshotHeight + kGap;
  }
  const int icon = aerie::tokens::kSizeIcon;
  const int title_h = title_->GetPreferredSize().height();
  const int text_x = kPadding + icon + kGap;
  favicon_->SetBoundsRect(GetMirroredRect(
      gfx::Rect(kPadding, y + (title_h - icon) / 2, icon, icon)));
  title_->SetBoundsRect(GetMirroredRect(
      gfx::Rect(text_x, y, width() - text_x - kPadding, title_h)));
  y += title_h;
  const int host_h = host_->GetPreferredSize().height();
  host_->SetBoundsRect(GetMirroredRect(
      gfx::Rect(text_x, y, width() - text_x - kPadding, host_h)));
  y += host_h;
  // The URL starts under the favicon, with the whole width, so that a long
  // address has room.
  if (url_->GetVisible()) {
    url_->SetBoundsRect(GetMirroredRect(
        gfx::Rect(kPadding, y, ContentWidth(),
                  url_->GetHeightForWidth(ContentWidth()))));
  }
}

void AerieHoverCardBody::OnPaint(gfx::Canvas* canvas) {
  if (snapshot_.isNull()) {
    return;
  }
  aerie_sidebar::PaintSnapshot(canvas, snapshot_, SnapshotBounds(),
                               aerie::tokens::kRadiusSm);
}

BEGIN_METADATA(AerieHoverCardBody)
END_METADATA

AerieHoverCardView::AerieHoverCardView(
    const aerie_sidebar::SidebarPalette& palette) {
  const aerie_sidebar::SidebarPalette popover =
      aerie_sidebar::PopoverPaletteOf(palette);
  surface_ = AddChildView(std::make_unique<AerieSidebarPeekSurface>());
  surface_->SetColours(popover);
  body_ = AddChildView(std::make_unique<AerieHoverCardBody>(popover));
  // The surface has a layer (for its shadow), and a layer draws over everything
  // its parent paints itself: so the body has one too.
  body_->SetPaintToLayer();
  body_->layer()->SetFillsBoundsOpaquely(false);
  GetViewAccessibility().SetIsIgnored(true);
}

AerieHoverCardView::~AerieHoverCardView() = default;

// static
int AerieHoverCardView::Inset() {
  return AerieSidebarPeekSurface::ShadowRoom();
}

gfx::Size AerieHoverCardView::CardSize() const {
  return body_->GetPreferredSize();
}

gfx::Size AerieHoverCardView::CalculatePreferredSize(
    const views::SizeBounds&) const {
  const gfx::Size card = CardSize();
  return gfx::Size(card.width() + 2 * Inset(), card.height() + 2 * Inset());
}

void AerieHoverCardView::Layout(PassKey) {
  surface_->SetBoundsRect(GetLocalBounds());
  const gfx::Size card = CardSize();
  body_->SetBoundsRect(gfx::Rect(Inset(), Inset(), card.width(), card.height()));
}

BEGIN_METADATA(AerieHoverCardView)
END_METADATA
