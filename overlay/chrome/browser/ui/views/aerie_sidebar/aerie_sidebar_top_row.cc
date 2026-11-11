// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_top_row.h"

#include <algorithm>
#include <utility>

#include "base/functional/bind.h"
#include "chrome/grit/generated_resources.h"
#include "components/aerie_design_tokens/aerie_tokens.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/color/color_id.h"
#include "cc/paint/paint_flags.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/image_button_factory.h"
#include "ui/views/controls/focus_ring.h"
#include "ui/views/controls/highlight_path_generator.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/vector_icons.h"
#include "ui/views/view_class_properties.h"

namespace {

// The toggle and the toolbar buttons share this square.
constexpr int kButtonSize = 28;
// The hit area of a window control circle.
constexpr int kControlHit = aerie::tokens::kSizeWindowControl +
                            2 * aerie::tokens::kSpace2;
// Alpha of the circle at rest and under the pointer, as in the mockup
// (sidebar.css, .wc-btn).
constexpr uint8_t kControlRestAlpha = 56;
constexpr uint8_t kControlHoverAlpha = 102;

// A window control: a plain circle. The platform's own look is not used on
// Linux, where the frame draws the controls on the far side of the window.
class WindowControlButton : public views::Button {
  METADATA_HEADER(WindowControlButton, views::Button)

 public:
  WindowControlButton(base::RepeatingClosure action,
                      int accessible_name_id,
                      bool is_close)
      : views::Button(std::move(action)), is_close_(is_close) {
    SetPreferredSize(gfx::Size(kControlHit, kControlHit));
    // Like the mockup: reachable by a screen reader, not a Tab stop.
    SetFocusBehavior(views::View::FocusBehavior::ACCESSIBLE_ONLY);
    const std::u16string name = l10n_util::GetStringUTF16(accessible_name_id);
    GetViewAccessibility().SetName(name);
    SetTooltipText(name);
    views::FocusRing::Install(this);
    views::InstallCircleHighlightPathGenerator(this);
  }

  void PaintButtonContents(gfx::Canvas* canvas) override {
    const bool hot = GetState() == STATE_HOVERED || GetState() == STATE_PRESSED;
    SkColor color;
    if (hot && is_close_) {
      color = GetColorProvider()->GetColor(ui::kColorSysError);
    } else {
      color = SkColorSetA(GetColorProvider()->GetColor(ui::kColorSysOnSurface),
                          hot ? kControlHoverAlpha : kControlRestAlpha);
    }
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(color);
    const gfx::PointF center(width() / 2.0f, height() / 2.0f);
    canvas->DrawCircle(center, aerie::tokens::kSizeWindowControl / 2.0f, flags);
  }

 private:
  const bool is_close_;
};

BEGIN_METADATA(WindowControlButton)
END_METADATA

}  // namespace

AerieSidebarTopRow::AerieSidebarTopRow(Callbacks callbacks) {
  layout_ = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(),
      aerie::tokens::kSpace4));
  layout_->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  SetProperty(views::kFlexBehaviorKey,
              views::FlexSpecification(views::MinimumFlexSizeRule::kPreferred,
                                       views::MaximumFlexSizeRule::kPreferred));
  GetViewAccessibility().SetRole(ax::mojom::Role::kToolbar);
  GetViewAccessibility().SetName(
      l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_TOP_ROW));

  // Window controls: hidden until the frame says it needs them.
  window_controls_ = AddChildView(std::make_unique<views::View>());
  auto* controls_layout =
      window_controls_->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets(),
          aerie::tokens::kSpace2));
  controls_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  window_controls_->GetViewAccessibility().SetRole(ax::mojom::Role::kGroup);
  window_controls_->GetViewAccessibility().SetName(
      l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_WINDOW_CONTROLS));
  window_controls_->AddChildView(std::make_unique<WindowControlButton>(
      callbacks.close_window, IDS_AERIE_SIDEBAR_CLOSE_WINDOW,
      /*is_close=*/true));
  window_controls_->AddChildView(std::make_unique<WindowControlButton>(
      callbacks.minimize_window, IDS_AERIE_SIDEBAR_MINIMIZE_WINDOW,
      /*is_close=*/false));
  window_controls_->AddChildView(std::make_unique<WindowControlButton>(
      callbacks.toggle_maximize_window, IDS_AERIE_SIDEBAR_MAXIMIZE_WINDOW,
      /*is_close=*/false));
  window_controls_->SetVisible(false);

  // The sidebar toggle (Ctrl+S).
  auto toggle = views::CreateVectorImageButton(callbacks.toggle_sidebar);
  views::SetImageFromVectorIconWithColor(
      toggle.get(), views::kSidebarLeftIcon, aerie::tokens::kSizeIcon,
      views::IconColors(ui::kColorSysOnSurface, ui::kColorSysOnSurface));
  toggle->SetPreferredSize(gfx::Size(kButtonSize, kButtonSize));
  toggle->SetTooltipText(l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_HIDE));
  toggle->GetViewAccessibility().SetName(
      l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_HIDE));
  views::InstallCircleHighlightPathGenerator(toggle.get());
  toggle_ = AddChildView(std::move(toggle));

  spacer_ = AddChildView(std::make_unique<views::View>());
  layout_->SetFlexForView(spacer_, 1);

  // Back, Forward and Reload go here, in a group of their own.
  navigation_ = AddChildView(std::make_unique<views::View>());
  auto* navigation_layout =
      navigation_->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets(),
          aerie::tokens::kSpace4));
  navigation_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  navigation_->GetViewAccessibility().SetRole(ax::mojom::Role::kGroup);
  navigation_->GetViewAccessibility().SetName(
      l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_NAVIGATION));

  Rebuild();
}

AerieSidebarTopRow::~AerieSidebarTopRow() = default;

views::View* AerieSidebarTopRow::GetDefaultFocusableChild() {
  return toggle_;
}

void AerieSidebarTopRow::SetLeadingInset(int inset) {
  leading_inset_ = std::max(0, inset);
  Rebuild();
}

void AerieSidebarTopRow::ShowWindowControls(bool show) {
  window_controls_->SetVisible(show);
  Rebuild();
}

bool AerieSidebarTopRow::window_controls_shown() const {
  return window_controls_->GetVisible();
}

void AerieSidebarTopRow::AdoptNavigationButton(views::View* button) {
  if (!button) {
    return;
  }
  // The button is owned by the toolbar until it is taken out of it.
  views::View* parent = button->parent();
  if (!parent) {
    return;
  }
  navigation_->AddChildView(parent->RemoveChildViewT(button));
  InvalidateLayout();
}

views::View* AerieSidebarTopRow::toggle_button() const {
  return toggle_;
}

bool AerieSidebarTopRow::IsPositionInWindowCaption(
    const gfx::Point& point) const {
  if (!GetLocalBounds().Contains(point)) {
    return false;
  }
  for (views::View* child : {static_cast<views::View*>(window_controls_),
                             static_cast<views::View*>(toggle_),
                             static_cast<views::View*>(navigation_)}) {
    if (!child->GetVisible()) {
      continue;
    }
    gfx::Point in_child = point;
    ConvertPointToTarget(this, child, &in_child);
    if (child->GetLocalBounds().Contains(in_child)) {
      // The group's own empty gaps are not buttons, but a click that near a
      // button is more likely meant for it.
      return false;
    }
  }
  return true;
}

void AerieSidebarTopRow::Rebuild() {
  layout_->set_inside_border_insets(gfx::Insets::TLBR(0, leading_inset_, 0, 0));
  InvalidateLayout();
}

BEGIN_METADATA(AerieSidebarTopRow)
END_METADATA
