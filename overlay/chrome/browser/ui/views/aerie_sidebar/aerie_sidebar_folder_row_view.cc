// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_folder_row_view.h"

#include <optional>
#include <utility>

#include "base/functional/bind.h"
#include "chrome/browser/ui/views/aerie_sidebar/vector_icons/vector_icons.h"
#include "chrome/grit/generated_resources.h"
#include "components/aerie_design_tokens/aerie_motion.h"
#include "components/aerie_design_tokens/aerie_tokens.h"
#include "components/aerie_sidebar/aerie_sidebar_motion.h"
#include "components/prefs/pref_service.h"
#include "components/vector_icons/vector_icons.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/compositor/layer.h"
#include "ui/events/event.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/gfx/geometry/transform_util.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/image_button_factory.h"
#include "ui/views/controls/focus_ring.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/view_utils.h"

namespace {
constexpr int kFaviconSize = aerie::tokens::kSizeIcon;
// The square of the funnel that opens a folder's sheet.
constexpr int kRowButtonSize = 20;

// Degrees in one full turn: the chevron's turn is a share of it.
constexpr float kDegreesPerTurn = 360.f;

constexpr int kDropLayersSlackMs = AerieSidebarRowView::kDropLayersSlackMs;

// The chevron of a folder row. At rest it paints the right-pointing icon turned
// by `turn` (a share of a full turn), so that a row that is open when it is made
// needs no layer. While the chevron animates it has a layer, the layer's
// transform carries the angle, and it paints unturned.
class FolderChevron : public views::ImageView {
  METADATA_HEADER(FolderChevron, views::ImageView)

 public:
  void SetTurn(float turn) {
    if (turn_ == turn) {
      return;
    }
    turn_ = turn;
    SchedulePaint();
  }
  float turn() const { return turn_; }

  void OnPaint(gfx::Canvas* canvas) override {
    if (turn_ == 0.f) {
      views::ImageView::OnPaint(canvas);
      return;
    }
    canvas->Save();
    canvas->Translate(gfx::Vector2d(width() / 2, height() / 2));
    canvas->sk_canvas()->rotate(turn_ * kDegreesPerTurn);
    canvas->Translate(gfx::Vector2d(-width() / 2, -height() / 2));
    views::ImageView::OnPaint(canvas);
    canvas->Restore();
  }

 private:
  float turn_ = 0.f;
};

BEGIN_METADATA(FolderChevron)
END_METADATA

gfx::Transform TurnTransform(float turn) {
  return gfx::TransformAboutPivot(
      gfx::PointF(kFaviconSize / 2.0f, kFaviconSize / 2.0f),
      [turn] {
        gfx::Transform rotation;
        rotation.RotateAboutZAxis(turn * kDegreesPerTurn);
        return rotation;
      }());
}
}  // namespace

AerieSidebarFolderRowView::AerieSidebarFolderRowView(
    Callbacks callbacks,
    const aerie_sidebar::SidebarPalette& palette)
    : AerieSidebarRowView(std::move(callbacks), palette) {
  views::View* content_area = content();
  // The folder glyph takes the favicon's place; the sleep badge never shows.
  sleep_badge_view()->SetVisible(false);
  auto chevron = std::make_unique<FolderChevron>();
  chevron->SetImageSize(gfx::Size(kFaviconSize, kFaviconSize));
  chevron->GetViewAccessibility().SetIsIgnored(true);
  chevron_ = content_area->AddChildViewAt(std::move(chevron), 0);
  // The dot of a shut folder that holds the open tab, after the name.
  auto dot = std::make_unique<views::View>();
  dot->SetPreferredSize(gfx::Size(aerie::tokens::kSizeFolderDot,
                                  aerie::tokens::kSizeFolderDot));
  dot->GetViewAccessibility().SetIsIgnored(true);
  dot->SetVisible(false);
  const std::optional<size_t> title_index = content_area->GetIndexOf(title_label());
  selection_dot_ = content_area->AddChildViewAt(
      std::move(dot),
      title_index ? *title_index + 1 : content_area->children().size());
  // The funnel of an auto-filing folder, after the name (issue #139).
  auto funnel = views::CreateVectorImageButton(base::BindRepeating(
      [](AerieSidebarFolderRowView* row) {
        if (row->callbacks().edit_folder) {
          row->callbacks().edit_folder.Run();
        }
      },
      base::Unretained(this)));
  funnel->SetPreferredSize(gfx::Size(kRowButtonSize, kRowButtonSize));
  funnel->SetVisible(false);
  funnel->SetFocusBehavior(views::View::FocusBehavior::NEVER);
  funnel->SetInstallFocusRingOnFocus(false);
  views::FocusRing::Remove(funnel.get());
  const std::optional<size_t> dot_index = content_area->GetIndexOf(selection_dot_);
  funnel_ = content_area->AddChildViewAt(
      std::move(funnel),
      dot_index ? *dot_index + 1 : content_area->children().size());
  // The pulse: an accent wash over the whole row that is at 0 opacity at rest.
  auto pulse = std::make_unique<views::View>();
  pulse->SetProperty(views::kViewIgnoredByLayoutKey, true);
  pulse->SetCanProcessEventsWithinSubtree(false);
  pulse->GetViewAccessibility().SetIsIgnored(true);
  pulse->SetPaintToLayer();
  pulse->layer()->SetFillsBoundsOpaquely(false);
  pulse->layer()->SetOpacity(0.f);
  pulse_ = content_area->AddChildView(std::move(pulse));
  rename_caption_label()->SetText(
      l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_FOLDER_RENAME_CAPTION));
  GetViewAccessibility().SetIsSelected(false);
  GetViewAccessibility().SetIsIgnored(false);
  SetPalette(palette);
  UpdateName();
}

AerieSidebarFolderRowView::~AerieSidebarFolderRowView() {
  chevron_motion_.Abort();
  pulse_motion_.Abort();
}

// static
AerieSidebarFolderRowView* AerieSidebarFolderRowView::From(
    AerieSidebarRowView* row) {
  return views::AsViewClass<AerieSidebarFolderRowView>(row);
}

bool AerieSidebarFolderRowView::is_folder() const {
  return true;
}

bool AerieSidebarFolderRowView::ShowsCloseButton() const {
  return false;
}

std::u16string AerieSidebarFolderRowView::AccessibleName() const {
  return folder_accessible_name_;
}

int AerieSidebarFolderRowView::RenameFieldNameId() const {
  return IDS_AERIE_SIDEBAR_FOLDER_RENAME_FIELD_NAME;
}

int AerieSidebarFolderRowView::RenameCaptionId() const {
  return IDS_AERIE_SIDEBAR_FOLDER_RENAME_CAPTION;
}

bool AerieSidebarFolderRowView::IsEditFolderClick(
    const ui::MouseEvent& event) const {
  // A double-click on a folder's icon opens the folder editor (issue #139).
  if (!event.IsOnlyLeftMouseButton() || event.GetClickCount() != 2 ||
      !callbacks().edit_folder) {
    return false;
  }
  views::ImageView* icon = favicon_view();
  gfx::Point in_icon = event.location();
  views::View::ConvertPointToTarget(this, icon, &in_icon);
  return icon->GetLocalBounds().Contains(in_icon);
}

void AerieSidebarFolderRowView::OnDensityChanged() {
  // The rail shows the folder glyph only (issue #184).
  chevron_->SetVisible(!icons_only());
  selection_dot_->SetVisible(holds_selected_tab_ && !icons_only());
  funnel_->SetVisible(auto_filing_ && !icons_only());
}

void AerieSidebarFolderRowView::LayoutKindViews() {
  if (pulse_) {
    pulse_->SetBoundsRect(gfx::Rect(0, 0, width(), row_height()));
  }
}

void AerieSidebarFolderRowView::ApplyKindPalette() {
  const aerie_sidebar::SidebarPalette& colors = palette();
  favicon_view()->SetImage(ui::ImageModel::FromVectorIcon(
      vector_icons::kFolderFilledIcon, colors.text_muted, icon_size()));
  chevron_->SetImage(ui::ImageModel::FromVectorIcon(
      vector_icons::kKeyboardArrowRightFlippableIcon, colors.text_muted,
      icon_size()));
  selection_dot_->SetBackground(views::CreateRoundedRectBackground(
      colors.accent, aerie::tokens::kSizeFolderDot / 2));
  for (views::Button::ButtonState state :
       {views::Button::STATE_NORMAL, views::Button::STATE_DISABLED}) {
    funnel_->SetImageModel(
        state, ui::ImageModel::FromVectorIcon(
                   aerie_sidebar::kAerieFolderRulesIcon, colors.text_muted,
                   icon_size()));
  }
  for (views::Button::ButtonState state :
       {views::Button::STATE_HOVERED, views::Button::STATE_PRESSED}) {
    funnel_->SetImageModel(
        state, ui::ImageModel::FromVectorIcon(
                   aerie_sidebar::kAerieFolderRulesIcon, colors.text,
                   icon_size()));
  }
  pulse_->SetBackground(views::CreateRoundedRectBackground(
      colors.accent, aerie::tokens::kRadiusMd));
}

views::View* AerieSidebarFolderRowView::chevron_view() const {
  return chevron_;
}

void AerieSidebarFolderRowView::SetAutoFiling(bool auto_filing,
                                              const std::u16string& tooltip) {
  auto_filing_ = auto_filing;
  funnel_->SetVisible(auto_filing && !icons_only());
  funnel_->SetTooltipText(tooltip);
  funnel_->GetViewAccessibility().SetName(tooltip);
}

void AerieSidebarFolderRowView::PlayFilingPulse(const PrefService* prefs) {
  const aerie_sidebar::PulseMotion motion = aerie_sidebar::AutoFilePulseMotion(
      aerie::motion::ShouldReduceMotion(prefs));
  pulse_motion_.Abort();
  pulse_timer_.Stop();
  ui::Layer* layer = pulse_->layer();
  layer->SetOpacity(0.f);
  // The durations are already the reduced ones when motion is reduced, so the
  // two steps go through with no prefs (AnimateLayer would cut a reduced
  // opacity animation to 90 ms, and the pulse is longer by design). Only the
  // opacity moves.
  aerie::motion::LayerMotion rise;
  rise.opacity = motion.peak_opacity;
  pulse_motion_ = aerie::motion::AnimateLayerForMs(
      layer, rise, motion.rise_ms, motion.rise_ms, motion.rise_easing, nullptr);
  pulse_timer_.Start(
      FROM_HERE, base::Milliseconds(motion.rise_ms),
      base::BindOnce(
          [](base::WeakPtr<AerieSidebarFolderRowView> row,
             aerie_sidebar::PulseMotion motion) {
            if (!row) {
              return;
            }
            aerie::motion::LayerMotion fall;
            fall.opacity = 0.f;
            row->pulse_motion_ = aerie::motion::AnimateLayerForMs(
                row->pulse_->layer(), fall, motion.fall_ms, motion.fall_ms,
                motion.fall_easing, nullptr);
          },
          folder_weak_factory_.GetWeakPtr(), motion));
}

void AerieSidebarFolderRowView::SetFolderState(
    const std::u16string& name,
    const std::u16string& accessible_name,
    bool expanded,
    bool holds_selected_tab,
    const PrefService* motion_prefs) {
  if (title_text() != name) {
    SetTitle(name);
  }
  folder_accessible_name_ = accessible_name;
  UpdateName();
  holds_selected_tab_ = holds_selected_tab && !expanded;
  selection_dot_->SetVisible(holds_selected_tab_ && !icons_only());
  const bool changed = !folder_state_applied_ || expanded_ != expanded;
  const bool animate = folder_state_applied_ && motion_prefs;
  folder_state_applied_ = true;
  expanded_ = expanded;
  if (expanded) {
    GetViewAccessibility().SetIsExpanded();
  } else {
    GetViewAccessibility().SetIsCollapsed();
  }
  if (!changed) {
    return;
  }
  auto* chevron = static_cast<FolderChevron*>(chevron_.get());
  const aerie_sidebar::ChevronMotion motion = aerie_sidebar::FolderChevronMotion(
      aerie::motion::ShouldReduceMotion(motion_prefs));
  const float target_turn = expanded ? motion.open_turn : 0.f;
  chevron_motion_.Abort();
  chevron_timer_.Stop();
  if (!animate || !motion.animate) {
    // At once: painted turned, no layer, no transform.
    if (chevron->layer()) {
      chevron->DestroyLayer();
    }
    chevron->SetTurn(target_turn);
    return;
  }
  // The turn: the layer carries the angle, from the angle the chevron has now
  // (a running turn that is reversed starts from where it got to).
  const gfx::Transform start = chevron->layer()
                                   ? chevron->layer()->transform()
                                   : TurnTransform(chevron->turn());
  if (!chevron->layer()) {
    chevron->SetPaintToLayer();
    chevron->layer()->SetFillsBoundsOpaquely(false);
  }
  chevron->SetTurn(0.f);
  chevron->layer()->SetTransform(start);
  const float open_turn = motion.open_turn;
  aerie::motion::LayerMotion target;
  target.transform = TurnTransform(expanded ? open_turn : 0.f);
  chevron_motion_ = aerie::motion::AnimateLayer(
      chevron->layer(), target, motion.duration, motion.easing, motion_prefs);
  chevron_timer_.Start(
      FROM_HERE, base::Milliseconds(motion.duration_ms + kDropLayersSlackMs),
      base::BindOnce(&AerieSidebarFolderRowView::EndChevronTurn,
                     base::Unretained(this)));
}

void AerieSidebarFolderRowView::EndChevronTurn() {
  auto* chevron = static_cast<FolderChevron*>(chevron_.get());
  chevron_motion_.Abort();
  if (chevron->layer()) {
    chevron->DestroyLayer();
  }
  chevron->SetTurn(expanded_ ? aerie_sidebar::FolderChevronMotion(false).open_turn
                             : 0.f);
}

BEGIN_METADATA(AerieSidebarFolderRowView)
END_METADATA
