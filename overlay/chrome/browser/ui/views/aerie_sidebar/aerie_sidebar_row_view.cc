// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_row_view.h"

#include <cmath>
#include <utility>

#include "base/functional/bind.h"
#include "build/build_config.h"
#include "chrome/grit/generated_resources.h"
#include "components/aerie_design_tokens/aerie_motion.h"
#include "components/aerie_design_tokens/aerie_tokens.h"
#include "components/aerie_sidebar/aerie_sidebar_motion.h"
#include "components/prefs/pref_service.h"
#include "components/vector_icons/vector_icons.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_id.h"
#include "ui/compositor/layer.h"
#include "ui/events/event.h"
#include "ui/gfx/animation/slide_animation.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/gfx/geometry/transform_util.h"
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
constexpr int kRowHeight = aerie::tokens::kSizeRowHeightComfortable;

// The row height animates on the design's `standard` curve, which is exactly
// this Tween type (aerie_motion.h).
constexpr gfx::Tween::Type kHeightTween = gfx::Tween::FAST_OUT_SLOW_IN_3;

// A transform that scales `scale` about the middle of a `size` box.
gfx::Transform ScaleAboutCenter(const gfx::Size& size, float scale) {
  return gfx::TransformAboutPivot(
      gfx::PointF(size.width() / 2.0f, size.height() / 2.0f),
      gfx::Transform::MakeScale(scale));
}

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

AerieSidebarRowView::AerieSidebarRowView(Callbacks callbacks)
    : callbacks_(std::move(callbacks)) {
  // Everything that is drawn is in the content view: it keeps the full row
  // height while the row itself grows or collapses (and clips), and its layer
  // is the one that scales, slides and fades.
  content_ = AddChildView(std::make_unique<views::View>());
  auto* layout = content_->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal,
      gfx::Insets::VH(0, aerie::tokens::kSpace8), aerie::tokens::kSpace8));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  content_->GetViewAccessibility().SetIsIgnored(true);

  favicon_ = content_->AddChildView(std::make_unique<views::ImageView>());
  favicon_->SetImageSize(gfx::Size(kFaviconSize, kFaviconSize));
  favicon_->GetViewAccessibility().SetIsIgnored(true);

  title_ = content_->AddChildView(std::make_unique<views::Label>(
      std::u16string(), views::style::CONTEXT_LABEL, views::style::STYLE_BODY_4));
  title_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  title_->SetElideBehavior(gfx::ELIDE_TAIL);
  title_->SetEnabledColor(ui::kColorSysOnSurface);
  title_->GetViewAccessibility().SetIsIgnored(true);
  layout->SetFlexForView(title_, 1);

  close_button_ = content_->AddChildView(views::CreateVectorImageButton(
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

  // The close button is a child: moving onto it must not count as leaving the
  // row, or the button would hide under the pointer.
  SetNotifyEnterExitOnChild(true);
  SetFocusBehavior(views::View::FocusBehavior::ALWAYS);
  // The focus ring is size-focus-ring-width thick and sits
  // size-focus-ring-offset away from the row (docs/sidebar-design.md,
  // "Rows and tiles"). The halo inset is the distance of the stroke's centre
  // from the row's edge, so it is minus offset minus half the width.
  views::FocusRing::Install(this);
  views::FocusRing* ring = views::FocusRing::Get(this);
  ring->SetOutsetFocusRingDisabled(false);
  ring->SetHaloThickness(aerie::tokens::kSizeFocusRingWidth);
  ring->SetHaloInset(-(aerie::tokens::kSizeFocusRingOffset +
                       aerie::tokens::kSizeFocusRingWidth / 2.0f));
  views::InstallRoundRectHighlightPathGenerator(this, gfx::Insets(),
                                                aerie::tokens::kRadiusMd);
  GetViewAccessibility().SetRole(ax::mojom::Role::kTreeItem);
  GetViewAccessibility().SetHierarchicalLevel(1);
  GetViewAccessibility().SetIsSelected(false);
  GetViewAccessibility().SetName(std::u16string());
}

AerieSidebarRowView::~AerieSidebarRowView() {
  // A row that goes away mid-motion must not leave an animation behind.
  content_motion_.Abort();
}

void AerieSidebarRowView::SetTabStop(bool tab_stop) {
  if (closing_) {
    return;
  }
  SetFocusBehavior(tab_stop ? views::View::FocusBehavior::ALWAYS
                            : views::View::FocusBehavior::ACCESSIBLE_ONLY);
}

bool AerieSidebarRowView::is_tab_stop() const {
  return GetFocusBehavior() == views::View::FocusBehavior::ALWAYS;
}

void AerieSidebarRowView::SetTitle(const std::u16string& title) {
  title_->SetText(title);
  GetViewAccessibility().SetName(title);
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

void AerieSidebarRowView::Layout(PassKey) {
  LayoutSuperclass<views::View>(this);
  // The content keeps its full height while the row grows or collapses.
  content_->SetBoundsRect(gfx::Rect(0, 0, width(), kRowHeight));
}

std::u16string AerieSidebarRowView::GetRenderedTooltipText(
    const gfx::Point& p) const {
  // The tooltip tells what the row cuts off: it shows only for an elided title.
  return title_->IsDisplayTextTruncated() ? std::u16string(title_->GetText())
                                          : std::u16string();
}

gfx::Size AerieSidebarRowView::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  return gfx::Size(
      available_size.width().value_or(aerie::tokens::kSizeSidebarMin),
      static_cast<int>(std::lround(kRowHeight * height_fraction_)));
}

bool AerieSidebarRowView::OnMousePressed(const ui::MouseEvent& event) {
  if (closing_) {
    return false;
  }
  return event.IsOnlyLeftMouseButton() || event.IsOnlyMiddleMouseButton();
}

void AerieSidebarRowView::OnMouseReleased(const ui::MouseEvent& event) {
  if (closing_ || !HitTestPoint(event.location())) {
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
  if (closing_) {
    return false;
  }
  if (IsCloseShortcut(event)) {
    callbacks_.key.Run(aerie_sidebar::ListKey::kDelete);
    return true;
  }
  if (event.IsControlDown() || event.IsAltDown() || event.IsCommandDown()) {
    return false;
  }
  switch (event.key_code()) {
    case ui::VKEY_RETURN:
    case ui::VKEY_SPACE:
      callbacks_.select.Run();
      return true;
    case ui::VKEY_DELETE:
      callbacks_.key.Run(aerie_sidebar::ListKey::kDelete);
      return true;
    case ui::VKEY_UP:
      callbacks_.key.Run(aerie_sidebar::ListKey::kUp);
      return true;
    case ui::VKEY_DOWN:
      callbacks_.key.Run(aerie_sidebar::ListKey::kDown);
      return true;
    case ui::VKEY_HOME:
      callbacks_.key.Run(aerie_sidebar::ListKey::kHome);
      return true;
    case ui::VKEY_END:
      callbacks_.key.Run(aerie_sidebar::ListKey::kEnd);
      return true;
    default:
      return false;
  }
}

bool AerieSidebarRowView::SkipDefaultKeyEventProcessing(
    const ui::KeyEvent& event) {
  // Ctrl+W is the browser's "close tab" accelerator, which closes the selected
  // tab. On a focused row it closes the row's own tab, so the row sees the key
  // first.
  return IsCloseShortcut(event) ||
         views::View::SkipDefaultKeyEventProcessing(event);
}

void AerieSidebarRowView::OnFocus() {
  views::View::OnFocus();
  UpdateState();
  if (callbacks_.focused) {
    callbacks_.focused.Run();
  }
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
  close_button_->SetVisible(!closing_ && (hovered_ || HasFocus()));
  // The content draws the background, so that it scales and fades with it.
  if (selected_) {
    content_->SetBackground(views::CreateRoundedRectBackground(
        ui::kColorSysTonalContainer, aerie::tokens::kRadiusMd));
  } else if (hovered_) {
    content_->SetBackground(views::CreateRoundedRectBackground(
        ui::kColorSysStateHoverOnSubtle, aerie::tokens::kRadiusMd));
  } else {
    content_->SetBackground(nullptr);
  }
  content_->SchedulePaint();
}


bool AerieSidebarRowView::height_animating() const {
  return height_animation_ && height_animation_->is_animating();
}

void AerieSidebarRowView::AnimateHeight(bool grow,
                                        int duration_ms,
                                        gfx::Tween::Type tween) {
  // The row clips while it is shorter than its content. The layer exists only
  // for the length of the motion.
  if (!layer()) {
    SetPaintToLayer();
    layer()->SetFillsBoundsOpaquely(false);
  }
  layer()->SetMasksToBounds(true);
  if (!height_animation_) {
    height_animation_ = std::make_unique<gfx::SlideAnimation>(this);
  }
  height_animation_->SetTweenType(tween);
  height_animation_->SetSlideDuration(base::Milliseconds(duration_ms));
  height_animation_->Reset(grow ? 0.0 : 1.0);
  height_fraction_ = grow ? 0.0 : 1.0;
  if (grow) {
    height_animation_->Show();
  } else {
    height_animation_->Hide();
  }
  PreferredSizeChanged();
}

void AerieSidebarRowView::FinishHeightAnimation() {
  if (layer()) {
    DestroyLayer();
  }
}

void AerieSidebarRowView::AnimationProgressed(const gfx::Animation* animation) {
  height_fraction_ = height_animation_->GetCurrentValue();
  PreferredSizeChanged();
}

void AerieSidebarRowView::AnimationEnded(const gfx::Animation* animation) {
  height_fraction_ = height_animation_->GetCurrentValue();
  PreferredSizeChanged();
  if (closing_) {
    if (closed_) {
      std::move(closed_).Run();
    }
    return;
  }
  FinishHeightAnimation();
}

void AerieSidebarRowView::PlayOpen(const PrefService* prefs) {
  const bool reduced = aerie::motion::ShouldReduceMotion(prefs);
  const aerie_sidebar::RowMotion motion = aerie_sidebar::OpenRowMotion(reduced);
  content_motion_.Abort();
  if (!content_->layer()) {
    content_->SetPaintToLayer();
    content_->layer()->SetFillsBoundsOpaquely(false);
  }
  ui::Layer* layer = content_->layer();
  aerie::motion::LayerMotion target;
  layer->SetOpacity(motion.from_opacity);
  target.opacity = motion.to_opacity;
  if (motion.animate_scale) {
    // The row has not been laid out yet: it will be as wide as its list.
    const int row_width = parent() ? parent()->width() : width();
    layer->SetTransform(ScaleAboutCenter(gfx::Size(row_width, kRowHeight),
                                         motion.from_scale));
    target.transform = gfx::Transform();
  }
  content_motion_ = aerie::motion::AnimateLayer(
      layer, target, motion.content_duration, motion.content_easing, prefs);
  if (motion.animate_height) {
    AnimateHeight(/*grow=*/true, motion.height_duration_ms, kHeightTween);
  }
}

void AerieSidebarRowView::PlayClose(const PrefService* prefs,
                                    int outer_edge_sign,
                                    base::OnceClosure done) {
  if (closing_) {
    return;
  }
  closing_ = true;
  closed_ = std::move(done);
  SetFocusBehavior(views::View::FocusBehavior::ACCESSIBLE_ONLY);
  GetViewAccessibility().SetIsIgnored(true);
  UpdateState();

  const bool reduced = aerie::motion::ShouldReduceMotion(prefs);
  const aerie_sidebar::RowMotion motion =
      aerie_sidebar::CloseRowMotion(outer_edge_sign, reduced);
  content_motion_.Abort();
  if (!content_->layer()) {
    content_->SetPaintToLayer();
    content_->layer()->SetFillsBoundsOpaquely(false);
  }
  aerie::motion::LayerMotion target;
  target.opacity = motion.to_opacity;
  if (motion.animate_slide) {
    target.transform = gfx::Transform::MakeTranslation(motion.slide_x, 0);
  }
  content_motion_ = aerie::motion::AnimateLayer(
      content_->layer(), target, motion.content_duration,
      motion.content_easing, prefs);

  // The height collapses once the content has gone. Under reduced motion it
  // snaps: the row is done when its content has faded.
  close_collapses_ = motion.animate_height;
  close_height_ms_ = motion.height_duration_ms;
  collapse_timer_.Start(
      FROM_HERE, base::Milliseconds(motion.content_duration_ms),
      base::BindOnce(&AerieSidebarRowView::CollapseAfterContent,
                     base::Unretained(this)));
}

void AerieSidebarRowView::CollapseAfterContent() {
  if (close_collapses_) {
    AnimateHeight(/*grow=*/false, close_height_ms_, kHeightTween);
    // A platform without rich animation ends the animation at once.
    return;
  }
  if (closed_) {
    std::move(closed_).Run();
  }
}

BEGIN_METADATA(AerieSidebarRowView)
END_METADATA
