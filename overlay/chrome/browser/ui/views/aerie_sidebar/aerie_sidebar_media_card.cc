// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_media_card.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/time/time.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_focus_ring.h"
#include "chrome/grit/generated_resources.h"
#include "components/aerie_design_tokens/aerie_tokens.h"
#include "components/aerie_sidebar/aerie_sidebar_motion.h"
#include "components/vector_icons/vector_icons.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/focus_ring.h"
#include "ui/views/style/typography.h"

AerieSidebarMediaCard::AerieSidebarMediaCard(base::RepeatingClosure play_pause,
                                             base::RepeatingClosure next,
                                             base::RepeatingClosure go_to_tab)
    : palette_{} {
  SetOrientation(views::BoxLayout::Orientation::kVertical);
  SetCrossAxisAlignment(views::BoxLayout::CrossAxisAlignment::kStretch);
  SetInsideBorderInsets(gfx::Insets::VH(aerie::tokens::kSpace8,
                                        aerie::tokens::kSpace12));
  SetBetweenChildSpacing(aerie::tokens::kSpace4);
  GetViewAccessibility().SetRole(ax::mojom::Role::kGroup);
  GetViewAccessibility().SetName(
      l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_MEDIA_NAME));

  title_ = AddChildView(std::make_unique<views::Label>(
      std::u16string(), views::style::CONTEXT_LABEL, views::style::STYLE_BODY_4));
  title_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  title_->SetElideBehavior(gfx::ELIDE_TAIL);
  site_ = AddChildView(std::make_unique<views::Label>(
      std::u16string(), views::style::CONTEXT_LABEL, views::style::STYLE_BODY_5));
  site_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  site_->SetElideBehavior(gfx::ELIDE_TAIL);

  auto* controls = AddChildView(std::make_unique<views::BoxLayoutView>());
  controls->SetOrientation(views::BoxLayout::Orientation::kHorizontal);
  controls->SetBetweenChildSpacing(aerie::tokens::kSpace4);
  controls->SetCrossAxisAlignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  play_pause_ = controls->AddChildView(
      std::make_unique<views::ImageButton>(std::move(play_pause)));
  PrepareIconButton(play_pause_);
  next_ = controls->AddChildView(
      std::make_unique<views::ImageButton>(std::move(next)));
  PrepareIconButton(next_);
  next_->GetViewAccessibility().SetName(
      l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_MEDIA_NEXT));
  next_->SetTooltipText(l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_MEDIA_NEXT));
  auto* spacer = controls->AddChildView(std::make_unique<views::View>());
  controls->SetFlexForView(spacer, 1);
  go_to_tab_ = controls->AddChildView(std::make_unique<views::LabelButton>(
      std::move(go_to_tab),
      l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_MEDIA_GO_TO_TAB)));
  go_to_tab_->SetFocusBehavior(views::View::FocusBehavior::ALWAYS);
  go_to_tab_->SetInstallFocusRingOnFocus(false);
  views::FocusRing::Remove(go_to_tab_);
  AerieSidebarFocusRing::Install(go_to_tab_, aerie::tokens::kRadiusMd);

  SetVisible(false);
}

AerieSidebarMediaCard::~AerieSidebarMediaCard() {
  motion_.Abort();
}

void AerieSidebarMediaCard::PrepareIconButton(views::ImageButton* button) {
  button->SetFocusBehavior(views::View::FocusBehavior::ALWAYS);
  button->SetPreferredSize(gfx::Size(aerie::tokens::kSizeRowHeightComfortable,
                                     aerie::tokens::kSizeRowHeightComfortable));
  button->SetImageHorizontalAlignment(views::ImageButton::ALIGN_CENTER);
  button->SetImageVerticalAlignment(views::ImageButton::ALIGN_MIDDLE);
  button->SetInstallFocusRingOnFocus(false);
  views::FocusRing::Remove(button);
  AerieSidebarFocusRing::Install(button, aerie::tokens::kRadiusMd);
}

void AerieSidebarMediaCard::SetPalette(
    const aerie_sidebar::SidebarPalette& palette) {
  palette_ = palette;
  UpdateColours();
}

void AerieSidebarMediaCard::UpdateColours() {
  SetBackground(views::CreateRoundedRectBackground(palette_.row_selected,
                                                   aerie::tokens::kRadiusMd));
  title_->SetEnabledColor(palette_.text);
  site_->SetEnabledColor(palette_.text_muted);
  go_to_tab_->SetEnabledTextColors(palette_.text);
  for (views::View* button : {static_cast<views::View*>(play_pause_.get()),
                              static_cast<views::View*>(next_.get()),
                              static_cast<views::View*>(go_to_tab_.get())}) {
    AerieSidebarFocusRing::Get(button)->SetStyle(palette_.accent,
                                                 palette_.focus_ring_width);
  }
  play_pause_->SetImageModel(
      views::Button::STATE_NORMAL,
      ui::ImageModel::FromVectorIcon(
          playing_ ? vector_icons::kPauseIcon : vector_icons::kPlayArrowIcon,
          palette_.text, aerie::tokens::kSizeIcon));
  next_->SetImageModel(
      views::Button::STATE_NORMAL,
      ui::ImageModel::FromVectorIcon(vector_icons::kSkipNextIcon,
                                     palette_.text, aerie::tokens::kSizeIcon));
  next_->SetImageModel(
      views::Button::STATE_DISABLED,
      ui::ImageModel::FromVectorIcon(vector_icons::kSkipNextIcon,
                                     palette_.text_muted,
                                     aerie::tokens::kSizeIcon));
  SchedulePaint();
}

void AerieSidebarMediaCard::SetContent(const Content& content) {
  title_->SetText(content.title);
  title_->SetTooltipText(content.title);
  site_->SetText(content.site);
  next_->SetEnabled(content.next_enabled);
  if (playing_ != content.playing) {
    playing_ = content.playing;
    UpdateColours();
  }
  const std::u16string verb = l10n_util::GetStringUTF16(
      playing_ ? IDS_AERIE_SIDEBAR_MEDIA_PAUSE : IDS_AERIE_SIDEBAR_MEDIA_PLAY);
  play_pause_->GetViewAccessibility().SetName(verb);
  play_pause_->SetTooltipText(verb);
}

void AerieSidebarMediaCard::Appear(const PrefService* prefs) {
  wanted_ = true;
  if (rail_) {
    return;
  }
  if (GetVisible() && !hide_timer_.IsRunning()) {
    return;
  }
  StartShow(prefs);
}

void AerieSidebarMediaCard::StartShow(const PrefService* prefs) {
  motion_.Abort();
  hide_timer_.Stop();
  SetVisible(true);
  const aerie_sidebar::ToastMotion motion = aerie_sidebar::ToastShowMotion(
      aerie::motion::ShouldReduceMotion(prefs));
  if (!layer()) {
    SetPaintToLayer();
    layer()->SetFillsBoundsOpaquely(false);
  }
  layer()->SetOpacity(motion.from_opacity);
  aerie::motion::LayerMotion target;
  target.opacity = motion.to_opacity;
  if (motion.animate_rise) {
    layer()->SetTransform(gfx::Transform::MakeTranslation(0, motion.rise_px));
    target.transform = gfx::Transform();
  } else {
    layer()->SetTransform(gfx::Transform());
  }
  motion_ = aerie::motion::AnimateLayer(layer(), target, motion.duration,
                                        motion.easing, prefs);
}

void AerieSidebarMediaCard::Dismiss(const PrefService* prefs) {
  wanted_ = false;
  if (!GetVisible() || !layer()) {
    SetVisible(false);
    return;
  }
  if (hide_timer_.IsRunning()) {
    return;
  }
  motion_.Abort();
  const aerie_sidebar::ToastMotion motion = aerie_sidebar::ToastHideMotion(
      aerie::motion::ShouldReduceMotion(prefs));
  aerie::motion::LayerMotion target;
  target.opacity = motion.to_opacity;
  motion_ = aerie::motion::AnimateLayer(layer(), target, motion.duration,
                                        motion.easing, prefs);
  // The card goes when the fade has ended.
  hide_timer_.Start(FROM_HERE, base::Milliseconds(motion.duration_ms),
                    base::BindOnce(&AerieSidebarMediaCard::FinishHide,
                                   base::Unretained(this)));
}

void AerieSidebarMediaCard::FinishHide() {
  motion_.Abort();
  hide_timer_.Stop();
  if (wanted_) {
    return;
  }
  SetVisible(false);
  if (layer()) {
    DestroyLayer();
  }
}

void AerieSidebarMediaCard::SetRail(bool rail) {
  if (rail_ == rail) {
    return;
  }
  rail_ = rail;
  motion_.Abort();
  hide_timer_.Stop();
  SetVisible(wanted_ && !rail_);
  if (layer()) {
    layer()->SetOpacity(1.f);
    layer()->SetTransform(gfx::Transform());
    if (!GetVisible()) {
      DestroyLayer();
    }
  }
}

BEGIN_METADATA(AerieSidebarMediaCard)
END_METADATA
