// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_archive_button.h"

#include <utility>

#include "base/functional/bind.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_focus_ring.h"
#include "chrome/browser/ui/views/aerie_sidebar/vector_icons/vector_icons.h"
#include "chrome/grit/generated_resources.h"
#include "components/aerie_design_tokens/aerie_tokens.h"
#include "components/aerie_sidebar/aerie_sidebar_motion.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/views/accessibility/view_accessibility.h"

namespace {

// A scale about the middle of a view of `size`.
gfx::Transform ScaleAboutCenter(const gfx::Size& size, float scale) {
  gfx::Transform transform;
  transform.Translate(size.width() / 2.0f, size.height() / 2.0f);
  transform.Scale(scale, scale);
  transform.Translate(-size.width() / 2.0f, -size.height() / 2.0f);
  return transform;
}

}  // namespace

AerieSidebarArchiveButton::AerieSidebarArchiveButton(PressedCallback callback)
    : views::LabelButton(std::move(callback),
                         l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_ARCHIVE_BUTTON)) {
  SetHorizontalAlignment(gfx::ALIGN_LEFT);
  SetImageLabelSpacing(aerie::tokens::kSpace8);
  SetPreferredSize(gfx::Size(0, aerie::tokens::kSizeRowHeightComfortable));
  SetTooltipText(l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_ARCHIVE_BUTTON));
  GetViewAccessibility().SetName(
      l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_ARCHIVE_BUTTON));
  // Chromium's ring takes only a colour role; the sidebar draws its own.
  SetInstallFocusRingOnFocus(false);
  views::FocusRing::Remove(this);
  AerieSidebarFocusRing::Install(this, aerie::tokens::kRadiusMd);
}

AerieSidebarArchiveButton::~AerieSidebarArchiveButton() {
  motion_.Abort();
}

void AerieSidebarArchiveButton::SetPalette(
    const aerie_sidebar::SidebarPalette& palette) {
  palette_ = palette;
  SetEnabledTextColors(palette_.text);
  SetImageModel(views::Button::STATE_NORMAL,
                ui::ImageModel::FromVectorIcon(aerie_sidebar::kAerieArchiveIcon,
                                               palette_.text_muted,
                                               aerie::tokens::kSizeIcon));
  AerieSidebarFocusRing::Get(this)->SetColor(palette_.accent);
  SchedulePaint();
}

void AerieSidebarArchiveButton::OnPaintBackground(gfx::Canvas* canvas) {
  views::LabelButton::OnPaintBackground(canvas);
  if (flashing_) {
    // The reduced-motion pulse: a wash of the selection colour. A static state.
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(palette_.row_selected);
    canvas->DrawRoundRect(gfx::RectF(GetLocalBounds()),
                          aerie::tokens::kRadiusMd, flags);
  }
}

void AerieSidebarArchiveButton::Pulse(const PrefService* prefs) {
  motion_.Abort();
  timer_.Stop();
  const bool reduced = aerie::motion::ShouldReduceMotion(prefs);
  const aerie_sidebar::ArchiveMotion plan =
      aerie_sidebar::ArchiveTabMotion(reduced);
  pulsing_ = true;
  if (!plan.animate_scale) {
    flashing_ = true;
    SchedulePaint();
    timer_.Start(FROM_HERE, base::Milliseconds(plan.pulse_ms),
                 base::BindOnce(&AerieSidebarArchiveButton::EndPulse,
                                base::Unretained(this)));
    return;
  }
  if (!layer()) {
    SetPaintToLayer();
    layer()->SetFillsBoundsOpaquely(false);
  }
  // Up for half the time, then back for the other half.
  aerie::motion::LayerMotion up;
  up.transform = ScaleAboutCenter(size(), plan.pulse_scale);
  motion_ = aerie::motion::AnimateLayerForMs(
      layer(), up, plan.pulse_ms / 2, plan.pulse_ms / 2, plan.pulse_easing,
      prefs);
  timer_.Start(FROM_HERE, base::Milliseconds(plan.pulse_ms / 2),
               base::BindOnce(&AerieSidebarArchiveButton::StartFallBack,
                              base::Unretained(this), prefs));
}

void AerieSidebarArchiveButton::StartFallBack(const PrefService* prefs) {
  if (!layer()) {
    EndPulse();
    return;
  }
  const aerie_sidebar::ArchiveMotion plan = aerie_sidebar::ArchiveTabMotion(
      aerie::motion::ShouldReduceMotion(prefs));
  aerie::motion::LayerMotion down;
  down.transform = gfx::Transform();
  motion_ = aerie::motion::AnimateLayerForMs(
      layer(), down, plan.pulse_ms - plan.pulse_ms / 2,
      plan.pulse_ms - plan.pulse_ms / 2, plan.pulse_easing, prefs);
  timer_.Start(FROM_HERE,
               base::Milliseconds(plan.pulse_ms - plan.pulse_ms / 2),
               base::BindOnce(&AerieSidebarArchiveButton::EndPulse,
                              base::Unretained(this)));
}

void AerieSidebarArchiveButton::EndPulse() {
  motion_.Abort();
  pulsing_ = false;
  if (flashing_) {
    flashing_ = false;
    SchedulePaint();
  }
  if (layer()) {
    DestroyLayer();
  }
}

BEGIN_METADATA(AerieSidebarArchiveButton)
END_METADATA
