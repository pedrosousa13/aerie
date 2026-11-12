// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_switch.h"

#include <utility>

#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_focus_ring.h"
#include "components/aerie_design_tokens/aerie_tokens.h"
#include "components/aerie_sidebar/aerie_sidebar_motion.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/compositor/layer.h"
#include "ui/events/event.h"
#include "cc/paint/paint_flags.h"
#include "components/prefs/pref_service.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/base/ui_base_types.h"

namespace {

// The track is 2 x 1 of the 16 px space step, the thumb a step in from each
// edge. All of it is the spacing scale; there is no size token for a switch.
constexpr int kTrackHeight = aerie::tokens::kSpace16;
constexpr int kTrackWidth = aerie::tokens::kSpace32;
constexpr int kThumbPad = aerie::tokens::kSpace2;
constexpr int kThumbSize = kTrackHeight - 2 * kThumbPad;

class RoundedFill : public views::View {
  METADATA_HEADER(RoundedFill, views::View)
 public:
  void SetFill(SkColor color) {
    SetBackground(views::CreateRoundedRectBackground(color, kTrackHeight / 2.0f));
  }
};

BEGIN_METADATA(RoundedFill)
END_METADATA

}  // namespace

AerieSidebarSwitch::AerieSidebarSwitch(
    const aerie_sidebar::SidebarPalette& palette,
    ChangedCallback changed)
    : palette_(palette), changed_(std::move(changed)) {
  SetFocusBehavior(FocusBehavior::ALWAYS);
  GetViewAccessibility().SetRole(ax::mojom::Role::kSwitch);
  // The accent track fades over the off track; its layer is what animates.
  auto accent = std::make_unique<RoundedFill>();
  accent->SetPaintToLayer();
  accent->layer()->SetFillsBoundsOpaquely(false);
  accent->layer()->SetOpacity(0.f);
  accent->SetCanProcessEventsWithinSubtree(false);
  accent->GetViewAccessibility().SetIsIgnored(true);
  accent_track_ = AddChildView(std::move(accent));
  auto thumb = std::make_unique<views::View>();
  thumb->SetPaintToLayer();
  thumb->layer()->SetFillsBoundsOpaquely(false);
  thumb->SetCanProcessEventsWithinSubtree(false);
  thumb->GetViewAccessibility().SetIsIgnored(true);
  thumb_ = AddChildView(std::move(thumb));
  AerieSidebarFocusRing::Install(this, kTrackHeight / 2.0f);
  SetPalette(palette);
  UpdateAccessibility();
}

AerieSidebarSwitch::~AerieSidebarSwitch() {
  thumb_motion_.Abort();
  track_motion_.Abort();
}

void AerieSidebarSwitch::SetPalette(
    const aerie_sidebar::SidebarPalette& palette) {
  palette_ = palette;
  static_cast<RoundedFill*>(accent_track_.get())->SetFill(palette_.accent);
  thumb_->SetBackground(views::CreateRoundedRectBackground(
      palette_.text, kThumbSize / 2.0f));
  AerieSidebarFocusRing::Get(this)->SetColor(palette_.accent);
  SchedulePaint();
}

int AerieSidebarSwitch::ThumbTravel() const {
  return kTrackWidth - kThumbSize - 2 * kThumbPad;
}

void AerieSidebarSwitch::SetOn(bool on, const PrefService* motion_prefs) {
  const bool changed = !state_applied_ || on_ != on;
  const bool animate = state_applied_ && motion_prefs;
  on_ = on;
  state_applied_ = true;
  UpdateAccessibility();
  if (changed) {
    ApplyState(animate, motion_prefs);
  }
}

void AerieSidebarSwitch::ApplyState(bool animate, const PrefService* prefs) {
  thumb_motion_.Abort();
  track_motion_.Abort();
  const gfx::Transform thumb_target =
      on_ ? gfx::Transform::MakeTranslation(ThumbTravel(), 0) : gfx::Transform();
  const float track_target = on_ ? 1.f : 0.f;
  if (!animate) {
    thumb_->layer()->SetTransform(thumb_target);
    accent_track_->layer()->SetOpacity(track_target);
    return;
  }
  const aerie_sidebar::SwitchMotion motion = aerie_sidebar::SwitchThumbMotion(
      aerie::motion::ShouldReduceMotion(prefs));
  aerie::motion::LayerMotion thumb;
  thumb.transform = thumb_target;
  thumb_motion_ = aerie::motion::AnimateLayer(thumb_->layer(), thumb,
                                              motion.duration, motion.easing,
                                              prefs);
  aerie::motion::LayerMotion track;
  track.opacity = track_target;
  track_motion_ = aerie::motion::AnimateLayer(accent_track_->layer(), track,
                                              motion.duration, motion.easing,
                                              prefs);
}

void AerieSidebarSwitch::UpdateAccessibility() {
  GetViewAccessibility().SetCheckedState(
      on_ ? ax::mojom::CheckedState::kTrue : ax::mojom::CheckedState::kFalse);
}

gfx::Size AerieSidebarSwitch::CalculatePreferredSize(
    const views::SizeBounds&) const {
  return gfx::Size(kTrackWidth, kTrackHeight);
}

void AerieSidebarSwitch::Layout(PassKey) {
  accent_track_->SetBoundsRect(gfx::Rect(0, 0, kTrackWidth, kTrackHeight));
  // The thumb sits at its off place; the layer's transform carries it to on.
  thumb_->SetBoundsRect(gfx::Rect(kThumbPad, kThumbPad, kThumbSize, kThumbSize));
  LayoutSuperclass<views::View>(this);
}

void AerieSidebarSwitch::OnPaint(gfx::Canvas* canvas) {
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setColor(palette_.control_hover);
  canvas->DrawRoundRect(gfx::Rect(0, 0, kTrackWidth, kTrackHeight),
                        kTrackHeight / 2.0f, flags);
}

void AerieSidebarSwitch::Toggle() {
  if (!GetEnabled()) {
    return;
  }
  const bool next = !on_;
  // The owner decides: it writes the model, and the model tells the sheet, which
  // calls SetOn again. The switch follows at once so that it never lags a click.
  SetOn(next, motion_prefs_);
  if (changed_) {
    changed_.Run(next);
  }
}

bool AerieSidebarSwitch::OnMousePressed(const ui::MouseEvent& event) {
  pressed_ = event.IsOnlyLeftMouseButton();
  return pressed_;
}

void AerieSidebarSwitch::OnMouseReleased(const ui::MouseEvent& event) {
  const bool inside = HitTestPoint(event.location());
  if (pressed_ && inside) {
    Toggle();
  }
  pressed_ = false;
}

bool AerieSidebarSwitch::OnKeyPressed(const ui::KeyEvent& event) {
  if (event.key_code() == ui::VKEY_SPACE || event.key_code() == ui::VKEY_RETURN) {
    Toggle();
    return true;
  }
  return false;
}

void AerieSidebarSwitch::OnFocus() {
  views::View::OnFocus();
  NotifyAccessibilityEventDeprecated(ax::mojom::Event::kFocus, true);
}

BEGIN_METADATA(AerieSidebarSwitch)
END_METADATA
