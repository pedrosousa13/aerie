// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_recent_tabs_overlay.h"

#include <algorithm>
#include <utility>

#include "cc/paint/paint_flags.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_peek_surface.h"
#include "chrome/grit/generated_resources.h"
#include "components/aerie_design_tokens/aerie_tokens.h"
#include "third_party/skia/include/core/SkRRect.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect_conversions.h"
#include "ui/gfx/geometry/skia_conversions.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/style/typography.h"
#include "ui/views/view_class_properties.h"

namespace {

// Width of the card, and the height of the area above its footer.
constexpr int kCardWidth = aerie::tokens::kSizeSwitcherCardWidth;
constexpr int kSnapshotHeight = aerie::tokens::kSizeSwitcherSnapshotHeight;
// Room in a card around its parts.
constexpr int kCardPadding = aerie::tokens::kSpace8;
// The icon in the snapshot area when there is no snapshot.
constexpr int kPlaceholderIcon = aerie::tokens::kSpace32;

}  // namespace

AerieRecentTabCard::AerieRecentTabCard(
    const aerie_sidebar::SidebarPalette& palette)
    : palette_(palette) {
  placeholder_ = AddChildView(std::make_unique<views::ImageView>());
  placeholder_->SetCanProcessEventsWithinSubtree(false);
  favicon_ = AddChildView(std::make_unique<views::ImageView>());
  favicon_->SetCanProcessEventsWithinSubtree(false);
  title_ = AddChildView(std::make_unique<views::Label>(
      std::u16string(), views::style::CONTEXT_LABEL, views::style::STYLE_BODY_4));
  title_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  title_->SetElideBehavior(gfx::ELIDE_TAIL);
  title_->SetEnabledColor(palette_.text);
  site_ = AddChildView(std::make_unique<views::Label>(
      std::u16string(), views::style::CONTEXT_LABEL, views::style::STYLE_BODY_5));
  site_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  site_->SetElideBehavior(gfx::ELIDE_TAIL);
  site_->SetEnabledColor(palette_.text_muted);
  GetViewAccessibility().SetRole(ax::mojom::Role::kListBoxOption);
  GetViewAccessibility().SetIsSelected(false);
}

AerieRecentTabCard::~AerieRecentTabCard() = default;

void AerieRecentTabCard::SetContent(const Content& content) {
  title_->SetText(content.title);
  site_->SetText(content.site);
  snapshot_ = content.snapshot;
  favicon_->SetImage(content.favicon);
  placeholder_->SetImage(content.favicon);
  placeholder_->SetImageSize(gfx::Size(kPlaceholderIcon, kPlaceholderIcon));
  placeholder_->SetVisible(snapshot_.isNull());
  GetViewAccessibility().SetName(l10n_util::GetStringFUTF16(
      IDS_AERIE_RECENT_TABS_CARD_NAME, content.title, content.site));
  InvalidateLayout();
  SchedulePaint();
}

void AerieRecentTabCard::SetSnapshot(const gfx::ImageSkia& snapshot) {
  snapshot_ = snapshot;
  placeholder_->SetVisible(snapshot_.isNull());
  SchedulePaint();
}

void AerieRecentTabCard::SetSelected(bool selected) {
  if (selected_ == selected) {
    return;
  }
  selected_ = selected;
  GetViewAccessibility().SetIsSelected(selected);
  SchedulePaint();
}

gfx::Size AerieRecentTabCard::CalculatePreferredSize(
    const views::SizeBounds&) const {
  const int footer = title_->GetPreferredSize().height() +
                     site_->GetPreferredSize().height();
  return gfx::Size(kCardWidth, kCardPadding + kSnapshotHeight + kCardPadding +
                                   footer + kCardPadding);
}

gfx::Rect AerieRecentTabCard::SnapshotBounds() const {
  return gfx::Rect(kCardPadding, kCardPadding, width() - 2 * kCardPadding,
                   kSnapshotHeight);
}

void AerieRecentTabCard::Layout(PassKey) {
  const gfx::Rect area = SnapshotBounds();
  placeholder_->SetBoundsRect(
      gfx::Rect(area.CenterPoint().x() - kPlaceholderIcon / 2,
                area.CenterPoint().y() - kPlaceholderIcon / 2, kPlaceholderIcon,
                kPlaceholderIcon));
  const int icon = aerie::tokens::kSizeIcon;
  const int footer_y = area.bottom() + kCardPadding;
  const int text_x = kCardPadding + icon + aerie::tokens::kSpace8;
  const int title_h = title_->GetPreferredSize().height();
  favicon_->SetBoundsRect(gfx::Rect(
      kCardPadding, footer_y + (title_h - icon) / 2, icon, icon));
  title_->SetBoundsRect(
      gfx::Rect(text_x, footer_y, width() - text_x - kCardPadding, title_h));
  site_->SetBoundsRect(gfx::Rect(text_x, footer_y + title_h,
                                 width() - text_x - kCardPadding,
                                 site_->GetPreferredSize().height()));
}

void AerieRecentTabCard::OnPaint(gfx::Canvas* canvas) {
  const gfx::RectF bounds(GetLocalBounds());
  if (selected_) {
    cc::PaintFlags fill;
    fill.setAntiAlias(true);
    fill.setColor(palette_.row_selected);
    canvas->DrawRoundRect(bounds, aerie::tokens::kRadiusMd, fill);
    // The ring of the Workspace accent, as the sidebar's focus ring.
    cc::PaintFlags ring;
    ring.setAntiAlias(true);
    ring.setStyle(cc::PaintFlags::kStroke_Style);
    ring.setStrokeWidth(palette_.focus_ring_width);
    ring.setColor(palette_.accent);
    gfx::RectF ring_bounds = bounds;
    ring_bounds.Inset(palette_.focus_ring_width / 2.f);
    canvas->DrawRoundRect(ring_bounds, aerie::tokens::kRadiusMd, ring);
  }
  const gfx::Rect area = SnapshotBounds();
  if (snapshot_.isNull()) {
    cc::PaintFlags quiet;
    quiet.setAntiAlias(true);
    quiet.setColor(palette_.row_hover);
    canvas->DrawRoundRect(gfx::RectF(area), aerie::tokens::kRadiusSm, quiet);
    return;
  }
  // The picture fills the area, cropped at the bottom and the sides to keep
  // its proportions, with rounded corners.
  canvas->Save();
  canvas->sk_canvas()->clipRRect(
      SkRRect::MakeRectXY(gfx::RectToSkRect(area), aerie::tokens::kRadiusSm,
                          aerie::tokens::kRadiusSm),
      /*doAntiAlias=*/true);
  const float scale =
      std::max(static_cast<float>(area.width()) / snapshot_.width(),
               static_cast<float>(area.height()) / snapshot_.height());
  const int source_w = std::min<int>(snapshot_.width(), area.width() / scale);
  const int source_h = std::min<int>(snapshot_.height(), area.height() / scale);
  const int source_x = (snapshot_.width() - source_w) / 2;
  canvas->DrawImageInt(snapshot_, source_x, 0, source_w, source_h, area.x(),
                       area.y(), area.width(), area.height(),
                       /*filter=*/true);
  canvas->Restore();
}

BEGIN_METADATA(AerieRecentTabCard)
END_METADATA

AerieSidebarRecentTabsOverlay::AerieSidebarRecentTabsOverlay(
    const aerie_sidebar::SidebarPalette& palette)
    : palette_(palette) {
  surface_ = AddChildView(std::make_unique<AerieSidebarPeekSurface>());
  surface_->SetColours(palette_);
  GetViewAccessibility().SetRole(ax::mojom::Role::kListBox);
  GetViewAccessibility().SetName(
      l10n_util::GetStringUTF16(IDS_AERIE_RECENT_TABS_NAME));
}

AerieSidebarRecentTabsOverlay::~AerieSidebarRecentTabsOverlay() = default;

void AerieSidebarRecentTabsOverlay::SetCards(
    const std::vector<AerieRecentTabCard::Content>& cards) {
  for (AerieRecentTabCard* card : cards_) {
    RemoveChildViewT(card);
  }
  cards_.clear();
  for (const AerieRecentTabCard::Content& content : cards) {
    auto* card = AddChildView(std::make_unique<AerieRecentTabCard>(palette_));
    card->SetContent(content);
    cards_.push_back(card);
  }
  selected_ = 0;
  InvalidateLayout();
}

void AerieSidebarRecentTabsOverlay::SetSelectedIndex(size_t index) {
  if (index >= cards_.size()) {
    return;
  }
  selected_ = index;
  for (size_t i = 0; i < cards_.size(); ++i) {
    cards_[i]->SetSelected(i == selected_);
  }
}

gfx::Size AerieSidebarRecentTabsOverlay::CalculatePreferredSize(
    const views::SizeBounds&) const {
  const int room = AerieSidebarPeekSurface::ShadowRoom() + aerie::tokens::kSpace16;
  int width = 0;
  int height = 0;
  for (const AerieRecentTabCard* card : cards_) {
    width += card->GetPreferredSize().width();
    height = std::max(height, card->GetPreferredSize().height());
  }
  if (!cards_.empty()) {
    width += static_cast<int>(cards_.size() - 1) * aerie::tokens::kSpace8;
  }
  return gfx::Size(width + 2 * room, height + 2 * room);
}

void AerieSidebarRecentTabsOverlay::Layout(PassKey) {
  surface_->SetBoundsRect(GetLocalBounds());
  const int room = AerieSidebarPeekSurface::ShadowRoom() + aerie::tokens::kSpace16;
  int x = room;
  for (AerieRecentTabCard* card : cards_) {
    const gfx::Size size = card->GetPreferredSize();
    card->SetBoundsRect(gfx::Rect(x, room, size.width(), size.height()));
    x += size.width() + aerie::tokens::kSpace8;
  }
}

BEGIN_METADATA(AerieSidebarRecentTabsOverlay)
END_METADATA
