// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_row_view.h"

#include <cmath>
#include <utility>

#include "base/functional/bind.h"
#include "build/build_config.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_focus_ring.h"
#include "chrome/browser/ui/views/aerie_sidebar/vector_icons/vector_icons.h"
#include "chrome/grit/generated_resources.h"
#include "components/aerie_design_tokens/aerie_motion.h"
#include "components/aerie_design_tokens/aerie_tokens.h"
#include "components/aerie_sidebar/aerie_sidebar_drag.h"
#include "components/aerie_sidebar/aerie_sidebar_motion.h"
#include "components/prefs/pref_service.h"
#include "components/vector_icons/vector_icons.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/compositor/layer.h"
#include "ui/events/event.h"
#include "ui/gfx/animation/slide_animation.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/gfx/geometry/transform_util.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/animation/ink_drop.h"
#include "ui/views/background.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/image_button_factory.h"
#include "ui/views/controls/focus_ring.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/style/typography.h"
#include "ui/views/widget/widget.h"

namespace {
constexpr int kFaviconSize = aerie::tokens::kSizeIcon;
// The moon badge of a sleeping tab (issue #179), from the tokens.
constexpr int kSleepBadgeSize = aerie::tokens::kSizeSleepBadge;
constexpr int kSleepBadgeOffset = aerie::tokens::kSizeSleepBadgeOffset;
constexpr int kRowHeight = aerie::tokens::kSizeRowHeightComfortable;

// A row keeps its drag layers this long after the motion's own duration, so the
// last frame is drawn before they go. Not a design value.
constexpr int kDropLayersSlackMs = 50;

// The row height animates on the design's `standard` curve, which is exactly
// this Tween type (aerie_motion.h).
constexpr gfx::Tween::Type kHeightTween = gfx::Tween::FAST_OUT_SLOW_IN_3;

// A transform that scales `scale` about the middle of a `size` box.
gfx::Transform ScaleAboutCenter(const gfx::Size& size, float scale) {
  return gfx::TransformAboutPivot(
      gfx::PointF(size.width() / 2.0f, size.height() / 2.0f),
      gfx::Transform::MakeScale(scale));
}

// The favicon and the moon badge that sits on its lower corner. The badge is
// beside the favicon, not in it, so it keeps its full colour while the favicon
// fades.
class FaviconHolder : public views::View {
  METADATA_HEADER(FaviconHolder, views::View)

 public:
  FaviconHolder(views::ImageView* favicon, views::ImageView* badge)
      : favicon_(AddChildView(favicon)), badge_(AddChildView(badge)) {}

  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    return gfx::Size(kFaviconSize, kFaviconSize);
  }
  void Layout(PassKey) override {
    favicon_->SetBoundsRect(gfx::Rect(0, 0, kFaviconSize, kFaviconSize));
    badge_->SetBoundsRect(gfx::Rect(kSleepBadgeOffset, kSleepBadgeOffset,
                                    kSleepBadgeSize, kSleepBadgeSize));
  }

 private:
  raw_ptr<views::ImageView> favicon_;
  raw_ptr<views::ImageView> badge_;
};

BEGIN_METADATA(FaviconHolder)
END_METADATA

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

AerieSidebarRowView::AerieSidebarRowView(
    Callbacks callbacks,
    const aerie_sidebar::SidebarPalette& palette)
    : callbacks_(std::move(callbacks)), palette_(palette) {
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

  auto favicon = std::make_unique<views::ImageView>();
  favicon->SetImageSize(gfx::Size(kFaviconSize, kFaviconSize));
  favicon->GetViewAccessibility().SetIsIgnored(true);
  auto badge = std::make_unique<views::ImageView>();
  badge->SetImageSize(gfx::Size(kSleepBadgeSize, kSleepBadgeSize));
  badge->GetViewAccessibility().SetIsIgnored(true);
  badge->SetVisible(false);
  favicon_ = favicon.get();
  sleep_badge_ = badge.get();
  content_->AddChildView(
      std::make_unique<FaviconHolder>(favicon.release(), badge.release()));

  title_ = content_->AddChildView(std::make_unique<views::Label>(
      std::u16string(), views::style::CONTEXT_LABEL, views::style::STYLE_BODY_4));
  title_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  title_->SetElideBehavior(gfx::ELIDE_TAIL);
  title_->SetEnabledColor(palette_.text);
  title_->GetViewAccessibility().SetIsIgnored(true);
  layout->SetFlexForView(title_, 1);

  // The "Back to pinned page" button, shown only on a pinned row that has left
  // its page. It is not a focus stop: the row's own keys are the keyboard way
  // (the context menu has no entry for it either; the button is a pointer
  // affordance, and a screen reader reaches it by its name).
  back_to_pinned_ = content_->AddChildView(views::CreateVectorImageButton(
      base::BindRepeating(
          [](AerieSidebarRowView* row) {
            if (row->callbacks_.back_to_pinned) {
              row->callbacks_.back_to_pinned.Run();
            }
          },
          base::Unretained(this))));
  back_to_pinned_->SetPreferredSize(gfx::Size(aerie::tokens::kSizeBackButton, aerie::tokens::kSizeBackButton));
  back_to_pinned_->SetVisible(false);
  back_to_pinned_->SetFocusBehavior(views::View::FocusBehavior::NEVER);
  back_to_pinned_->SetInstallFocusRingOnFocus(false);
  views::FocusRing::Remove(back_to_pinned_);
  back_to_pinned_->GetViewAccessibility().SetName(
      l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_BACK_TO_PINNED));
  back_to_pinned_->SetTooltipText(
      l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_BACK_TO_PINNED));

  close_button_ = content_->AddChildView(views::CreateVectorImageButton(
      base::BindRepeating(
          [](AerieSidebarRowView* row) { row->callbacks_.close.Run(); },
          base::Unretained(this))));
  close_button_->SetPreferredSize(gfx::Size(20, 20));
  close_button_->SetVisible(false);
  close_button_->SetFocusBehavior(views::View::FocusBehavior::NEVER);
  close_button_->SetInstallFocusRingOnFocus(false);
  views::FocusRing::Remove(close_button_);
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
  // "Rows and tiles"). Chromium's own ring takes only a colour role, so the
  // row draws it in the Workspace accent.
  AerieSidebarFocusRing::Install(this, aerie::tokens::kRadiusMd);
  GetViewAccessibility().SetRole(ax::mojom::Role::kTreeItem);
  GetViewAccessibility().SetHierarchicalLevel(1);
  GetViewAccessibility().SetIsSelected(false);
  GetViewAccessibility().SetName(std::u16string());
  set_context_menu_controller(this);
  SetPalette(palette_);
}

AerieSidebarRowView::~AerieSidebarRowView() {
  // A row that goes away mid-motion must not leave an animation behind.
  content_motion_.Abort();
  row_motion_.Abort();
  lift_motion_.Abort();
  sleep_motion_.Abort();
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

void AerieSidebarRowView::SetPalette(
    const aerie_sidebar::SidebarPalette& palette) {
  palette_ = palette;
  title_->SetEnabledColor(palette_.text);
  // The close icon is muted at rest and the text colour under the pointer
  // (the mockup's `.ibtn`).
  for (views::Button::ButtonState state :
       {views::Button::STATE_NORMAL, views::Button::STATE_DISABLED}) {
    close_button_->SetImageModel(
        state, ui::ImageModel::FromVectorIcon(vector_icons::kCloseSmallIcon,
                                              palette_.text_muted, kFaviconSize));
  }
  for (views::Button::ButtonState state :
       {views::Button::STATE_HOVERED, views::Button::STATE_PRESSED}) {
    close_button_->SetImageModel(
        state, ui::ImageModel::FromVectorIcon(vector_icons::kCloseSmallIcon,
                                              palette_.text, kFaviconSize));
  }
  views::InkDrop::Get(close_button_)->SetBaseColor(palette_.text);
  for (views::Button::ButtonState state :
       {views::Button::STATE_NORMAL, views::Button::STATE_DISABLED}) {
    back_to_pinned_->SetImageModel(
        state, ui::ImageModel::FromVectorIcon(vector_icons::kUndoIcon,
                                              palette_.text_muted, kFaviconSize));
  }
  for (views::Button::ButtonState state :
       {views::Button::STATE_HOVERED, views::Button::STATE_PRESSED}) {
    back_to_pinned_->SetImageModel(
        state, ui::ImageModel::FromVectorIcon(vector_icons::kUndoIcon,
                                              palette_.text, kFaviconSize));
  }
  views::InkDrop::Get(back_to_pinned_)->SetBaseColor(palette_.text);
  AerieSidebarFocusRing::Get(this)->SetColor(palette_.accent);
  sleep_badge_->SetImage(ui::ImageModel::FromVectorIcon(
      aerie_sidebar::kAerieSleepBadgeIcon, palette_.text, kSleepBadgeSize));
  UpdateState();
}

void AerieSidebarRowView::SetBackToPinnedVisible(bool visible) {
  back_to_pinned_->SetVisible(visible && !closing_);
}

void AerieSidebarRowView::SetCloseTooltip(const std::u16string& text) {
  close_button_->SetTooltipText(text);
  close_button_->GetViewAccessibility().SetName(text);
}

void AerieSidebarRowView::SetTitle(const std::u16string& title) {
  title_->SetText(title);
  title_text_ = title;
  UpdateName();
}

void AerieSidebarRowView::UpdateName() {
  GetViewAccessibility().SetName(
      sleeping_ ? l10n_util::GetStringFUTF16(IDS_AERIE_SIDEBAR_ROW_ASLEEP_NAME,
                                             title_text_)
                : title_text_);
}

void AerieSidebarRowView::SetSleeping(bool sleeping,
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

void AerieSidebarRowView::ApplySleepLook(bool animate,
                                         const PrefService* prefs) {
  sleep_motion_.Abort();
  desaturate_timer_.Stop();
  if (!sleeping_) {
    // The favicon of an awake tab has no layer, as before.
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
    // An instant swap: both looks at once.
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
  // The desaturate is a static filter that follows the fade; it is not
  // animated.
  desaturate_timer_.Start(
      FROM_HERE,
      base::Milliseconds(aerie::tokens::GetDurationMs(motion.duration, false) +
                         kDropLayersSlackMs),
      base::BindOnce(
          [](AerieSidebarRowView* row) {
            if (row->sleeping_ && row->favicon_->layer()) {
              row->favicon_->layer()->SetLayerGrayscale(1.f);
            }
          },
          base::Unretained(this)));
}

void AerieSidebarRowView::ShowContextMenuForViewImpl(
    views::View* source,
    const gfx::Point& point,
    ui::mojom::MenuSourceType source_type) {
  if (!closing_ && callbacks_.context_menu) {
    callbacks_.context_menu.Run(point, source_type);
  }
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
  pressed_ = event.IsOnlyLeftMouseButton();
  drag_cancelled_ = false;
  press_point_ = event.root_location();
  return event.IsOnlyLeftMouseButton() || event.IsOnlyMiddleMouseButton();
}

bool AerieSidebarRowView::OnMouseDragged(const ui::MouseEvent& event) {
  if (closing_ || !pressed_) {
    return false;
  }
  // The root location does not move with the row's own layer, so the travel is
  // the pointer's, not the pointer's relative to a row that is following it.
  const gfx::Point point = event.root_location();
  const int dx = point.x() - press_point_.x();
  const int dy = point.y() - press_point_.y();
  if (!dragging_) {
    if (!aerie_sidebar::ExceedsDragThreshold(dx, dy)) {
      return true;
    }
    if (!callbacks_.drag_begin || !callbacks_.drag_begin.Run()) {
      return true;
    }
    dragging_ = true;
    // The most recent accelerator for Esc is tried first, so the drag takes
    // it before the browser's own.
    AddAccelerator(ui::Accelerator(ui::VKEY_ESCAPE, ui::EF_NONE));
  }
  last_dy_ = dy;
  callbacks_.drag_update.Run(dy);
  return true;
}

void AerieSidebarRowView::OnMouseReleased(const ui::MouseEvent& event) {
  pressed_ = false;
  if (dragging_) {
    const views::Widget* widget = GetWidget();
    const bool outside =
        widget && !widget->GetRootView()->GetLocalBounds().Contains(
                      event.root_location());
    FinishDrag(event.root_location().y() - press_point_.y(),
               /*cancelled=*/false, outside);
    return;
  }
  if (drag_cancelled_) {
    drag_cancelled_ = false;
    return;
  }
  if (closing_ || !HitTestPoint(event.location())) {
    return;
  }
  if (event.IsOnlyLeftMouseButton()) {
    callbacks_.select.Run();
  } else if (event.IsOnlyMiddleMouseButton()) {
    callbacks_.close.Run();
  }
}

void AerieSidebarRowView::OnMouseCaptureLost() {
  pressed_ = false;
  if (dragging_) {
    FinishDrag(last_dy_, /*cancelled=*/true, /*outside=*/false);
  }
}

bool AerieSidebarRowView::AcceleratorPressed(
    const ui::Accelerator& accelerator) {
  if (!dragging_ || accelerator.key_code() != ui::VKEY_ESCAPE) {
    return false;
  }
  pressed_ = false;
  drag_cancelled_ = true;
  FinishDrag(last_dy_, /*cancelled=*/true, /*outside=*/false);
  return true;
}

void AerieSidebarRowView::FinishDrag(int dy, bool cancelled, bool outside) {
  dragging_ = false;
  RemoveAccelerator(ui::Accelerator(ui::VKEY_ESCAPE, ui::EF_NONE));
  callbacks_.drag_end.Run(dy, cancelled, outside);
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
    case ui::VKEY_BACK:
      // "Back to pinned page" from the keyboard (issue #180): Backspace on a
      // pinned row that has left its page. Chromium's Alt+Left would collide
      // with the browser's Back, so it is not used.
      // Only the bare key: Shift+Backspace and the others do nothing (Ctrl,
      // Alt and Command are already refused above).
      if (!event.IsShiftDown() && back_to_pinned_->GetVisible() &&
          callbacks_.back_to_pinned) {
        callbacks_.back_to_pinned.Run();
        return true;
      }
      return false;
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
        palette_.row_selected, aerie::tokens::kRadiusMd));
  } else if (hovered_) {
    content_->SetBackground(views::CreateRoundedRectBackground(
        palette_.row_hover, aerie::tokens::kRadiusMd));
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
  if (layer() && !drag_layers_) {
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

gfx::Transform AerieSidebarRowView::LiftTransform(float scale) const {
  return ScaleAboutCenter(gfx::Size(width(), kRowHeight), scale);
}

void AerieSidebarRowView::EnsureDragLayers() {
  settle_timer_.Stop();
  if (!layer()) {
    SetPaintToLayer();
    layer()->SetFillsBoundsOpaquely(false);
  }
  if (!content_->layer()) {
    content_->SetPaintToLayer();
    content_->layer()->SetFillsBoundsOpaquely(false);
  }
  drag_layers_ = true;
}

void AerieSidebarRowView::DropDragLayers() {
  settle_timer_.Stop();
  drag_layers_ = false;
  // A layer that the height animation needs stays; it drops its own.
  if (layer() && !height_animating()) {
    DestroyLayer();
  }
}

void AerieSidebarRowView::BeginLift(const PrefService* prefs) {
  EnsureDragLayers();
  row_motion_.Abort();
  lift_motion_.Abort();
  // The lifted row is above the rows it passes.
  if (layer()->parent()) {
    layer()->parent()->StackAtTop(layer());
  }
  const aerie_sidebar::DragMotion motion =
      aerie_sidebar::RowDragMotion(aerie::motion::ShouldReduceMotion(prefs));
  if (motion.animate_lift) {
    aerie::motion::LayerMotion target;
    target.transform = LiftTransform(motion.lift_scale);
    lift_motion_ =
        aerie::motion::AnimateLayer(content_->layer(), target,
                                    motion.lift_duration, motion.lift_easing,
                                    prefs);
  }
}

void AerieSidebarRowView::SetDragOffset(int dy) {
  EnsureDragLayers();
  row_motion_.Abort();
  drag_offset_ = dy;
  layer()->SetTransform(gfx::Transform::MakeTranslation(0, dy));
}

void AerieSidebarRowView::ShiftTo(int dy, const PrefService* prefs) {
  EnsureDragLayers();
  row_motion_.Abort();
  drag_offset_ = dy;
  const aerie_sidebar::DragMotion motion =
      aerie_sidebar::RowDragMotion(aerie::motion::ShouldReduceMotion(prefs));
  aerie::motion::LayerMotion target;
  target.transform = gfx::Transform::MakeTranslation(0, dy);
  // Reduced motion sets the transform at once: AnimateLayer never animates it.
  row_motion_ = aerie::motion::AnimateLayer(layer(), target,
                                            motion.shift_duration,
                                            motion.shift_easing, prefs);
  if (dy == 0) {
    // A row back at its slot gives its layer up when the motion has ended.
    settle_timer_.Start(
        FROM_HERE,
        base::Milliseconds(
            aerie::tokens::GetDurationMs(motion.shift_duration, false) +
            kDropLayersSlackMs),
        base::BindOnce(&AerieSidebarRowView::DropDragLayers,
                       base::Unretained(this)));
  }
}

void AerieSidebarRowView::Settle(int from_dy, const PrefService* prefs) {
  EnsureDragLayers();
  row_motion_.Abort();
  lift_motion_.Abort();
  const bool reduced = aerie::motion::ShouldReduceMotion(prefs);
  const aerie_sidebar::DragMotion motion = aerie_sidebar::RowDragMotion(reduced);
  layer()->SetTransform(gfx::Transform::MakeTranslation(0, from_dy));
  drag_offset_ = 0;
  aerie::motion::LayerMotion rest;
  rest.transform = gfx::Transform();
  row_motion_ = aerie::motion::AnimateLayerForMs(
      layer(), rest, motion.settle_duration_ms,
      motion.settle_reduced_duration_ms, motion.settle_easing, prefs);
  lift_motion_ = aerie::motion::AnimateLayer(content_->layer(), rest,
                                             motion.lift_duration,
                                             motion.lift_easing, prefs);
  if (!motion.animate_settle) {
    // The drop is instant; nothing is left to wait for.
    DropDragLayers();
    return;
  }
  settle_timer_.Start(
      FROM_HERE,
      base::Milliseconds(motion.settle_duration_ms + kDropLayersSlackMs),
      base::BindOnce(&AerieSidebarRowView::DropDragLayers,
                     base::Unretained(this)));
}

void AerieSidebarRowView::ResetDrag() {
  row_motion_.Abort();
  lift_motion_.Abort();
  drag_offset_ = 0;
  if (layer()) {
    layer()->SetTransform(gfx::Transform());
  }
  if (content_->layer() && drag_layers_) {
    content_->layer()->SetTransform(gfx::Transform());
  }
  DropDragLayers();
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
