// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_row_view.h"

#include <utility>

#include "chrome/grit/generated_resources.h"
#include "components/aerie_design_tokens/aerie_tokens.h"
#include "components/vector_icons/vector_icons.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_id.h"
#include "ui/events/event.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/image_button_factory.h"
#include "ui/views/controls/focus_ring.h"
#include "ui/views/controls/highlight_path_generator.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/style/typography.h"

namespace {
constexpr int kFaviconSize = aerie::tokens::kSizeIcon;
}  // namespace

AerieSidebarRowView::AerieSidebarRowView(Callbacks callbacks)
    : callbacks_(std::move(callbacks)) {
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal,
      gfx::Insets::VH(0, aerie::tokens::kSpace8), aerie::tokens::kSpace8));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  favicon_ = AddChildView(std::make_unique<views::ImageView>());
  favicon_->SetImageSize(gfx::Size(kFaviconSize, kFaviconSize));
  favicon_->GetViewAccessibility().SetIsIgnored(true);

  title_ = AddChildView(std::make_unique<views::Label>(
      std::u16string(), views::style::CONTEXT_LABEL, views::style::STYLE_BODY_4));
  title_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  title_->SetElideBehavior(gfx::ELIDE_TAIL);
  title_->SetEnabledColor(ui::kColorSysOnSurface);
  title_->GetViewAccessibility().SetIsIgnored(true);
  layout->SetFlexForView(title_, 1);

  close_button_ = AddChildView(views::CreateVectorImageButton(
      base::BindRepeating(
          [](AerieSidebarRowView* row) { row->callbacks_.close.Run(); },
          base::Unretained(this))));
  views::SetImageFromVectorIconWithColor(
      close_button_, vector_icons::kCloseSmallIcon, kFaviconSize,
      views::IconColors(ui::kColorSysOnSurface, ui::kColorSysOnSurface));
  close_button_->SetPreferredSize(gfx::Size(20, 20));
  close_button_->SetVisible(false);
  close_button_->SetFocusBehavior(views::View::FocusBehavior::NEVER);
  close_button_->GetViewAccessibility().SetName(
      l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_CLOSE_TAB));
  close_button_->SetTooltipText(
      l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_CLOSE_TAB));

  SetFocusBehavior(views::View::FocusBehavior::ALWAYS);
  views::FocusRing::Install(this);
  views::FocusRing::Get(this)->SetOutsetFocusRingDisabled(false);
  views::InstallRoundRectHighlightPathGenerator(this, gfx::Insets(),
                                                aerie::tokens::kRadiusMd);
  GetViewAccessibility().SetRole(ax::mojom::Role::kTreeItem);
  GetViewAccessibility().SetName(std::u16string());
}

AerieSidebarRowView::~AerieSidebarRowView() = default;

void AerieSidebarRowView::SetTitle(const std::u16string& title) {
  title_->SetText(title);
  GetViewAccessibility().SetName(title);
  SetTooltipText(title);
}

void AerieSidebarRowView::SetFavicon(const ui::ImageModel& favicon) {
  favicon_->SetImage(favicon);
}

void AerieSidebarRowView::SetSelected(bool selected) {
  if (selected_ == selected) {
    return;
  }
  selected_ = selected;
  GetViewAccessibility().SetIsSelected(selected);
  UpdateState();
}

gfx::Size AerieSidebarRowView::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  return gfx::Size(available_size.width().value_or(0),
                   aerie::tokens::kSizeRowHeightComfortable);
}

bool AerieSidebarRowView::OnMousePressed(const ui::MouseEvent& event) {
  return event.IsOnlyLeftMouseButton() || event.IsOnlyMiddleMouseButton();
}

void AerieSidebarRowView::OnMouseReleased(const ui::MouseEvent& event) {
  if (!HitTestPoint(event.location())) {
    return;
  }
  if (event.IsOnlyLeftMouseButton()) {
    callbacks_.select.Run();
  } else if (event.IsOnlyMiddleMouseButton()) {
    callbacks_.close.Run();
  }
}

void AerieSidebarRowView::OnMouseEntered(const ui::MouseEvent& event) {
  hovered_ = true;
  UpdateState();
}

void AerieSidebarRowView::OnMouseExited(const ui::MouseEvent& event) {
  hovered_ = false;
  UpdateState();
}

bool AerieSidebarRowView::OnKeyPressed(const ui::KeyEvent& event) {
  switch (event.key_code()) {
    case ui::VKEY_RETURN:
    case ui::VKEY_SPACE:
      callbacks_.select.Run();
      return true;
    case ui::VKEY_DELETE:
      callbacks_.close.Run();
      return true;
    default:
      return false;
  }
}

void AerieSidebarRowView::OnFocus() {
  views::View::OnFocus();
  UpdateState();
}

void AerieSidebarRowView::OnBlur() {
  views::View::OnBlur();
  UpdateState();
}

void AerieSidebarRowView::OnThemeChanged() {
  views::View::OnThemeChanged();
  UpdateState();
}

void AerieSidebarRowView::UpdateState() {
  close_button_->SetVisible(hovered_ || HasFocus());
  if (selected_) {
    SetBackground(views::CreateRoundedRectBackground(
        ui::kColorSysTonalContainer, aerie::tokens::kRadiusMd));
  } else if (hovered_) {
    SetBackground(views::CreateRoundedRectBackground(
        ui::kColorSysStateHoverOnSubtle, aerie::tokens::kRadiusMd));
  } else {
    SetBackground(nullptr);
  }
  SchedulePaint();
}

BEGIN_METADATA(AerieSidebarRowView)
END_METADATA
