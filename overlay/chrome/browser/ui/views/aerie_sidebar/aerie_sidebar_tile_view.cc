// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_tile_view.h"

#include <utility>

#include "base/functional/bind.h"
#include "build/build_config.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_focus_ring.h"
#include "chrome/browser/ui/views/aerie_sidebar/vector_icons/vector_icons.h"
#include "chrome/grit/generated_resources.h"
#include "components/aerie_design_tokens/aerie_motion.h"
#include "components/aerie_design_tokens/aerie_tokens.h"
#include "components/aerie_sidebar/aerie_sidebar_motion.h"
#include "components/prefs/pref_service.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/compositor/layer.h"
#include "ui/events/event.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/controls/image_view.h"

namespace {

constexpr int kFaviconSize = aerie::tokens::kSizeFaviconComfortable;
constexpr int kSleepBadgeSize = aerie::tokens::kSizeSleepBadge;
// The badge sits on the lower right corner of the favicon, as on a row.
constexpr int kSleepBadgeOffset = aerie::tokens::kSizeSleepBadgeOffset;
// The same slack as the rows keep before they drop a layer.
constexpr int kLayerSlackMs = 50;

bool IsCloseShortcut(const ui::KeyEvent& event) {
  if (event.key_code() != ui::VKEY_W) {
    return false;
  }
#if BUILDFLAG(IS_MAC)
  return event.IsCommandDown() && !event.IsControlDown();
#else
  return event.IsControlDown() && !event.IsShiftDown() && !event.IsAltDown();
#endif
}

}  // namespace

AerieSidebarTileView::AerieSidebarTileView(
    Callbacks callbacks,
    const aerie_sidebar::SidebarPalette& palette)
    : callbacks_(std::move(callbacks)), palette_(palette) {
  auto favicon = std::make_unique<views::ImageView>();
  favicon->SetImageSize(gfx::Size(kFaviconSize, kFaviconSize));
  favicon->GetViewAccessibility().SetIsIgnored(true);
  auto badge = std::make_unique<views::ImageView>();
  badge->SetImageSize(gfx::Size(kSleepBadgeSize, kSleepBadgeSize));
  badge->GetViewAccessibility().SetIsIgnored(true);
  badge->SetVisible(false);
  favicon_ = AddChildView(std::move(favicon));
  sleep_badge_ = AddChildView(std::move(badge));

  SetFocusBehavior(views::View::FocusBehavior::ALWAYS);
  AerieSidebarFocusRing::Install(this, aerie::tokens::kRadiusMd);
  GetViewAccessibility().SetRole(ax::mojom::Role::kButton);
  GetViewAccessibility().SetName(std::u16string());
  set_context_menu_controller(this);
  SetPalette(palette_);
}

AerieSidebarTileView::~AerieSidebarTileView() {
  sleep_motion_.Abort();
  flip_motion_.Abort();
}

void AerieSidebarTileView::SetPalette(
    const aerie_sidebar::SidebarPalette& palette) {
  palette_ = palette;
  AerieSidebarFocusRing::Get(this)->SetColor(palette_.accent);
  sleep_badge_->SetImage(ui::ImageModel::FromVectorIcon(
      aerie_sidebar::kAerieSleepBadgeIcon, palette_.text, kSleepBadgeSize));
  UpdateState();
}

void AerieSidebarTileView::SetTitle(const std::u16string& title) {
  title_text_ = title;
  UpdateName();
}

void AerieSidebarTileView::UpdateName() {
  // The shown title, then ", asleep", then ", renamed".
  std::u16string name = title_text_;
  if (sleeping_) {
    name = l10n_util::GetStringFUTF16(IDS_AERIE_SIDEBAR_ROW_ASLEEP_NAME, name);
  }
  if (renamed_) {
    name = l10n_util::GetStringFUTF16(IDS_AERIE_SIDEBAR_ROW_RENAMED_NAME, name);
  }
  GetViewAccessibility().SetName(name);
}

void AerieSidebarTileView::SetRenamed(bool renamed,
                                      const std::u16string& renamed_tooltip) {
  renamed_tooltip_ = renamed_tooltip;
  if (renamed_ == renamed) {
    return;
  }
  renamed_ = renamed;
  UpdateName();
}

void AerieSidebarTileView::SetFavicon(const ui::ImageModel& favicon) {
  favicon_->SetImage(favicon);
}

void AerieSidebarTileView::SetSelected(bool selected) {
  if (selected_ == selected) {
    return;
  }
  selected_ = selected;
  UpdateState();
}

void AerieSidebarTileView::SetSleeping(
    bool sleeping,
    const PrefService* reduced_motion_prefs) {
  if (sleep_look_applied_ && sleeping_ == sleeping) {
    return;
  }
  const bool animate = sleep_look_applied_ && sleeping && reduced_motion_prefs;
  sleep_look_applied_ = true;
  sleeping_ = sleeping;
  UpdateName();
  sleep_badge_->SetVisible(sleeping);
  ApplySleepLook(animate, reduced_motion_prefs);
}

void AerieSidebarTileView::ApplySleepLook(bool animate,
                                          const PrefService* prefs) {
  sleep_motion_.Abort();
  desaturate_timer_.Stop();
  if (!sleeping_) {
    if (favicon_->layer()) {
      favicon_->DestroyLayer();
    }
    return;
  }
  if (!favicon_->layer()) {
    favicon_->SetPaintToLayer();
    favicon_->layer()->SetFillsBoundsOpaquely(false);
  }
  ui::Layer* layer = favicon_->layer();
  const aerie_sidebar::SleepMotion motion = aerie_sidebar::SleepFaviconMotion(
      aerie::motion::ShouldReduceMotion(prefs));
  if (!animate || !motion.animate_fade) {
    layer->SetOpacity(motion.asleep_opacity);
    layer->SetLayerGrayscale(1.f);
    return;
  }
  layer->SetOpacity(1.f);
  layer->SetLayerGrayscale(0.f);
  aerie::motion::LayerMotion target;
  target.opacity = motion.asleep_opacity;
  sleep_motion_ = aerie::motion::AnimateLayer(layer, target, motion.duration,
                                              motion.easing, prefs);
  desaturate_timer_.Start(
      FROM_HERE,
      base::Milliseconds(aerie::tokens::GetDurationMs(motion.duration, false) +
                         kLayerSlackMs),
      base::BindOnce(
          [](AerieSidebarTileView* tile) {
            if (tile->sleeping_ && tile->favicon_->layer()) {
              tile->favicon_->layer()->SetLayerGrayscale(1.f);
            }
          },
          base::Unretained(this)));
}

void AerieSidebarTileView::SetTabStop(bool tab_stop) {
  SetFocusBehavior(tab_stop ? views::View::FocusBehavior::ALWAYS
                            : views::View::FocusBehavior::ACCESSIBLE_ONLY);
}

bool AerieSidebarTileView::is_tab_stop() const {
  return GetFocusBehavior() == views::View::FocusBehavior::ALWAYS;
}

void AerieSidebarTileView::PlayFlipFrom(const gfx::Vector2d& delta,
                                        const PrefService* prefs) {
  if (delta.IsZero()) {
    return;
  }
  flip_motion_.Abort();
  flip_timer_.Stop();
  if (!layer()) {
    SetPaintToLayer();
    layer()->SetFillsBoundsOpaquely(false);
  }
  layer()->SetTransform(gfx::Transform::MakeTranslation(delta.x(), delta.y()));
  const aerie_sidebar::PinMotion motion =
      aerie_sidebar::PinFlipMotion(aerie::motion::ShouldReduceMotion(prefs));
  aerie::motion::LayerMotion rest;
  rest.transform = gfx::Transform();
  flip_motion_ = aerie::motion::AnimateLayerForMs(
      layer(), rest, motion.moved_duration_ms, motion.moved_reduced_duration_ms,
      motion.moved_easing, prefs);
  flip_timer_.Start(
      FROM_HERE,
      base::Milliseconds(motion.animate ? motion.moved_duration_ms + kLayerSlackMs
                                        : kLayerSlackMs),
      base::BindOnce(
          [](AerieSidebarTileView* tile) {
            if (tile->layer()) {
              tile->DestroyLayer();
            }
          },
          base::Unretained(this)));
}

void AerieSidebarTileView::Layout(PassKey) {
  LayoutSuperclass<views::View>(this);
  favicon_->SetBoundsRect(
      gfx::Rect((width() - kFaviconSize) / 2, (height() - kFaviconSize) / 2,
                kFaviconSize, kFaviconSize));
  sleep_badge_->SetBoundsRect(gfx::Rect(
      favicon_->x() + kFaviconSize - kSleepBadgeSize / 2 - 1,
      favicon_->y() + kFaviconSize - kSleepBadgeSize / 2 - 1, kSleepBadgeSize,
      kSleepBadgeSize));
}

std::u16string AerieSidebarTileView::GetRenderedTooltipText(
    const gfx::Point& p) const {
  std::u16string tooltip =
      sleeping_ ? l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_TILE_ASLEEP)
                : title_text_;
  if (renamed_ && !renamed_tooltip_.empty()) {
    tooltip += u"\n" + renamed_tooltip_;
  }
  return tooltip;
}

bool AerieSidebarTileView::OnMousePressed(const ui::MouseEvent& event) {
  suppress_release_ = false;
  pressed_ = event.IsOnlyLeftMouseButton();
  if (event.IsOnlyLeftMouseButton() && event.GetClickCount() == 2 &&
      callbacks_.begin_rename) {
    pressed_ = false;
    suppress_release_ = true;
    callbacks_.begin_rename.Run();
    return true;
  }
  return event.IsOnlyLeftMouseButton() || event.IsOnlyMiddleMouseButton();
}

void AerieSidebarTileView::OnMouseReleased(const ui::MouseEvent& event) {
  pressed_ = false;
  if (suppress_release_) {
    // The release of the double-click that opened the rename popover.
    suppress_release_ = false;
    return;
  }
  if (!HitTestPoint(event.location())) {
    return;
  }
  if (event.IsOnlyLeftMouseButton()) {
    callbacks_.select.Run();
  } else if (event.IsOnlyMiddleMouseButton()) {
    callbacks_.close.Run();
  }
}

void AerieSidebarTileView::OnMouseEntered(const ui::MouseEvent& event) {
  hovered_ = true;
  UpdateState();
}

void AerieSidebarTileView::OnMouseExited(const ui::MouseEvent& event) {
  hovered_ = false;
  UpdateState();
}

bool AerieSidebarTileView::OnKeyPressed(const ui::KeyEvent& event) {
  if (IsCloseShortcut(event)) {
    callbacks_.key.Run(aerie_sidebar::GridKey::kDelete);
    return true;
  }
  if (event.IsAltDown() || event.IsCommandDown()) {
    return false;
  }
  const bool control = event.IsControlDown();
  switch (event.key_code()) {
    case ui::VKEY_F2:
      if (control || event.IsShiftDown() || !callbacks_.begin_rename) {
        return false;
      }
      callbacks_.begin_rename.Run();
      return true;
    case ui::VKEY_RETURN:
    case ui::VKEY_SPACE:
      if (control) {
        return false;
      }
      callbacks_.select.Run();
      return true;
    case ui::VKEY_DELETE:
      callbacks_.key.Run(aerie_sidebar::GridKey::kDelete);
      return true;
    case ui::VKEY_LEFT:
      callbacks_.key.Run(aerie_sidebar::GridKey::kLeft);
      return true;
    case ui::VKEY_RIGHT:
      callbacks_.key.Run(aerie_sidebar::GridKey::kRight);
      return true;
    case ui::VKEY_UP:
      callbacks_.key.Run(aerie_sidebar::GridKey::kUp);
      return true;
    case ui::VKEY_DOWN:
      callbacks_.key.Run(aerie_sidebar::GridKey::kDown);
      return true;
    case ui::VKEY_HOME:
      callbacks_.key.Run(control ? aerie_sidebar::GridKey::kGridStart
                                 : aerie_sidebar::GridKey::kRowStart);
      return true;
    case ui::VKEY_END:
      callbacks_.key.Run(control ? aerie_sidebar::GridKey::kGridEnd
                                 : aerie_sidebar::GridKey::kRowEnd);
      return true;
    default:
      return false;
  }
}

bool AerieSidebarTileView::SkipDefaultKeyEventProcessing(
    const ui::KeyEvent& event) {
  // As on a row: Ctrl+W on a focused tile is for the tile's own tab.
  return IsCloseShortcut(event) ||
         views::View::SkipDefaultKeyEventProcessing(event);
}

void AerieSidebarTileView::OnFocus() {
  views::View::OnFocus();
  UpdateState();
  if (callbacks_.focused) {
    callbacks_.focused.Run();
  }
}

void AerieSidebarTileView::OnBlur() {
  views::View::OnBlur();
  UpdateState();
}

void AerieSidebarTileView::OnThemeChanged() {
  views::View::OnThemeChanged();
  UpdateState();
}

void AerieSidebarTileView::ShowContextMenuForViewImpl(
    views::View* source,
    const gfx::Point& point,
    ui::mojom::MenuSourceType source_type) {
  if (callbacks_.context_menu) {
    callbacks_.context_menu.Run(point, source_type);
  }
}

void AerieSidebarTileView::UpdateState() {
  // A tile is always a tile: the resting fill is the row hover colour, and the
  // selected tile and the hovered one take the selected fill.
  const SkColor fill = (selected_ || hovered_) ? palette_.row_selected
                                               : palette_.row_hover;
  SetBackground(
      views::CreateRoundedRectBackground(fill, aerie::tokens::kRadiusMd));
  SchedulePaint();
}

BEGIN_METADATA(AerieSidebarTileView)
END_METADATA
