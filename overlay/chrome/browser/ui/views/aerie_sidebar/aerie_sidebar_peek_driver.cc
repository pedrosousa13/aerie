// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_peek_driver.h"

#include <algorithm>

#include "base/task/single_thread_task_runner.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_view.h"
#include "components/aerie_design_tokens/aerie_tokens.h"
#include "components/aerie_sidebar/aerie_sidebar_density.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_floating.h"
#include "components/aerie_sidebar/aerie_sidebar_layout.h"
#include "components/aerie_sidebar/aerie_sidebar_motion.h"
#include "components/prefs/pref_service.h"
#include "ui/base/accelerators/accelerator_manager.h"
#include "ui/compositor/layer.h"
#include "ui/display/screen.h"
#include "ui/events/event.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace {

// How often the screen cursor is read while the sidebar is on screen and the
// pointer is outside the window. The design gives no token for it: a web page
// only sees where the pointer left, and a native build has to ask. 100 ms is
// six frames at 60 Hz, short enough that leaving the grace distance hides the
// sidebar while the keep-open time still runs. The poll exists only in that
// state (WantsCursorPoll) and stops with it.
constexpr base::TimeDelta kCursorPollInterval = base::Milliseconds(100);

ui::Accelerator KeepOpenAccelerator() {
  return ui::Accelerator(ui::VKEY_S, ui::EF_CONTROL_DOWN | ui::EF_ALT_DOWN);
}

}  // namespace

AerieSidebarPeekDriver::AerieSidebarPeekDriver(AerieSidebarView* view)
    : view_(view),
      controller_(base::SingleThreadTaskRunner::GetCurrentDefault(),
                  aerie_sidebar::PeekTimingsFor(false)) {
  controller_.AddObserver(this);
}

AerieSidebarPeekDriver::~AerieSidebarPeekDriver() {
  controller_.RemoveObserver(this);
  SetActive(false);
}

views::View* AerieSidebarPeekDriver::Region() const {
  // AerieSidebarView -> the content area of the region -> the region.
  return view_->parent() ? view_->parent()->parent() : nullptr;
}

views::View* AerieSidebarPeekDriver::Page() const {
  views::View* region = Region();
  return region ? region->parent() : nullptr;
}

int AerieSidebarPeekDriver::OuterSign() const {
  return view_->side_on_right() ? 1 : -1;
}

gfx::Rect AerieSidebarPeekDriver::WindowInScreen() const {
  views::View* page = Page();
  return page ? page->GetBoundsInScreen() : gfx::Rect();
}

// The footprint of the floating sidebar: its column and the inset between it
// and the window edge, so that a pointer crossing the gap is still over it.
gfx::Rect AerieSidebarPeekDriver::FootprintInScreen() const {
  gfx::Rect window = WindowInScreen();
  const int width = view_->column_width() + view_->density_sizes().inset;
  return OuterSign() > 0 ? gfx::Rect(window.right() - width, window.y(), width,
                                     window.height())
                         : gfx::Rect(window.x(), window.y(), width,
                                     window.height());
}

gfx::Rect AerieSidebarPeekDriver::HotZoneInScreen() const {
  gfx::Rect window = WindowInScreen();
  const int width = aerie::tokens::kSizeHotZone;
  return OuterSign() > 0 ? gfx::Rect(window.right() - width, window.y(), width,
                                     window.height())
                         : gfx::Rect(window.x(), window.y(), width,
                                     window.height());
}

void AerieSidebarPeekDriver::SetActive(bool active) {
  if (active == active_) {
    return;
  }
  active_ = active;
  views::Widget* widget = view_->GetWidget();
  if (active_) {
    ApplyTimings();
    controller_.Reset();
    if (widget) {
      widget_observation_.Observe(widget);
      monitor_ = views::EventMonitor::CreateWindowMonitor(
          this, widget->GetNativeWindow(),
          {ui::EventType::kMouseMoved, ui::EventType::kMouseExited});
      focus_manager_ = widget->GetFocusManager();
      if (focus_manager_) {
        focus_manager_->AddFocusChangeListener(this);
        // Ctrl+Alt+S keeps the peek open. It exists only while the sidebar is
        // auto-hidden, so that it is not a shortcut of the other densities.
        focus_manager_->RegisterAccelerator(
            KeepOpenAccelerator(), ui::AcceleratorManager::kNormalPriority,
            this);
      }
    }
    // Out of the layout and off screen until the pointer comes.
    view_->SetVisible(false);
    SetFloatingWidth(0);
    return;
  }
  monitor_.reset();
  poll_timer_.Stop();
  if (focus_manager_) {
    focus_manager_->RemoveFocusChangeListener(this);
    focus_manager_->UnregisterAccelerator(KeepOpenAccelerator(), this);
    focus_manager_ = nullptr;
  }
  motion_.Abort();
  controller_.Reset();
  f6_open_ = false;
  f6_focus_entered_ = false;
  focus_hold_.Reset();
  omnibox_hold_.Reset();
  bubble_hold_.Reset();
  widget_observation_.Reset();
  bubble_observation_.Reset();
  tracked_bubble_ = nullptr;
  SetFloatingWidth(0);
  if (views::View* region = Region()) {
    if (region->layer()) {
      region->layer()->SetTransform(gfx::Transform());
      region->layer()->SetOpacity(1.f);
    }
  }
  view_->SetVisible(true);
}

void AerieSidebarPeekDriver::ApplyTimings() {
  controller_.SetTimings(aerie_sidebar::PeekTimingsFor(
      aerie::motion::ShouldReduceMotion(view_->profile_prefs())));
}

void AerieSidebarPeekDriver::SetFloatingWidth(int width) {
  views::View* region = Region();
  if (!region || region->GetProperty(aerie_sidebar::kFloatingWidth) == width) {
    return;
  }
  region->SetProperty(aerie_sidebar::kFloatingWidth, width);
  region->InvalidateLayout();
  if (views::View* page = Page()) {
    page->InvalidateLayout();
  }
}

void AerieSidebarPeekDriver::OnEvent(const ui::Event& event) {
  if (!active_ || !monitor_) {
    return;
  }
  if (event.type() == ui::EventType::kMouseExited) {
    HandlePointerExit();
    return;
  }
  HandlePointer(monitor_->GetLastMouseLocation());
}

void AerieSidebarPeekDriver::HandlePointer(const gfx::Point& screen_point) {
  if (!active_) {
    return;
  }
  ApplyTimings();
  const gfx::Rect window = WindowInScreen();
  if (!window.Contains(screen_point)) {
    // Outside the window: the distance to the sidebar decides.
    const gfx::Rect footprint = FootprintInScreen();
    const int dx = std::max({0, footprint.x() - screen_point.x(),
                             screen_point.x() - footprint.right()});
    const int dy = std::max({0, footprint.y() - screen_point.y(),
                             screen_point.y() - footprint.bottom()});
    HoldForBubble();
    controller_.SetPointerOutsideWindow(dx, dy);
    UpdateCursorPoll();
    return;
  }
  const bool visible = controller_.visible();
  const bool over = visible && FootprintInScreen().Contains(screen_point);
  const bool hot = HotZoneInScreen().Contains(screen_point);
  if (!over && !hot) {
    // About to leave: a bubble open on the sidebar (the Guard panel) counts.
    HoldForBubble();
  }
  controller_.SetPointerInsideWindow();
  controller_.SetPointerOverSidebar(over);
  controller_.SetPointerInHotZone(hot);
  UpdateCursorPoll();
}

void AerieSidebarPeekDriver::HandlePointerExit() {
  // The screen cursor says where it went; a web page would only see the exit
  // point.
  RecheckPointer();
}

gfx::Point AerieSidebarPeekDriver::CursorPoint() const {
  return cursor_for_testing_ ? *cursor_for_testing_
                             : display::Screen::Get()->GetCursorScreenPoint();
}

void AerieSidebarPeekDriver::RecheckPointer() {
  HandlePointer(CursorPoint());
}

void AerieSidebarPeekDriver::OnWidgetActivationChanged(views::Widget* widget,
                                                       bool active) {
  // A window that loses activation gets no more pointer events, and the cursor
  // can leave it without one: the sidebar must not stay open for want of an
  // event. Look at where the cursor is; if it is outside, the poll takes over.
  if (active_ && widget == view_->GetWidget() && !active) {
    RecheckPointer();
  }
}

void AerieSidebarPeekDriver::UpdateCursorPoll() {
  if (controller_.WantsCursorPoll()) {
    if (!poll_timer_.IsRunning()) {
      poll_timer_.Start(FROM_HERE, kCursorPollInterval,
                        base::BindRepeating(
                            &AerieSidebarPeekDriver::RecheckPointer,
                            base::Unretained(this)));
    }
  } else {
    poll_timer_.Stop();
  }
}

void AerieSidebarPeekDriver::OnPeekShow() {
  SlideTo(/*show=*/true);
}

void AerieSidebarPeekDriver::OnPeekHide() {
  SlideTo(/*show=*/false);
}

void AerieSidebarPeekDriver::SlideTo(bool show) {
  views::View* region = Region();
  if (!region || !region->layer()) {
    return;
  }
  const PrefService* prefs = view_->profile_prefs();
  const bool reduced = aerie::motion::ShouldReduceMotion(prefs);
  const int width = view_->column_width();
  const aerie_sidebar::SlideMotion slide =
      show ? aerie_sidebar::PeekShowMotion(width, OuterSign(), reduced)
           : aerie_sidebar::PeekHideMotion(width, OuterSign(), reduced);
  if (show) {
    const bool was_off = !view_->GetVisible();
    view_->SetVisible(true);
    SetFloatingWidth(width);
    if (views::Widget* widget = view_->GetWidget()) {
      // F6 focuses the sidebar right after this: it needs its bounds now.
      widget->LayoutRootViewIfNecessary();
    }
    if (was_off) {
      if (slide.animate_transform) {
        region->layer()->SetTransform(
            gfx::Transform::MakeTranslation(slide.hidden_x, 0));
      }
      if (slide.animate_opacity) {
        region->layer()->SetOpacity(slide.from_opacity);
      }
    }
  }
  // A reversing slide starts from the offset the layer has now.
  aerie::motion::LayerMotion target;
  if (slide.animate_transform) {
    target.transform = show ? gfx::Transform()
                            : gfx::Transform::MakeTranslation(slide.hidden_x, 0);
  }
  if (slide.animate_opacity) {
    target.opacity = slide.to_opacity;
  }
  motion_.Abort();
  motion_ = aerie::motion::AnimateLayer(region->layer(), target, slide.duration,
                                        slide.easing, prefs);
}

void AerieSidebarPeekDriver::OnPeekStateChanged(aerie_sidebar::PeekState from,
                                                aerie_sidebar::PeekState to) {
  using aerie_sidebar::PeekState;
  if (to == PeekState::kHidden && active_) {
    FinishHidden();
  }
  if (to == PeekState::kHidden || to == PeekState::kArming) {
    f6_open_ = false;
    f6_focus_entered_ = false;
    focus_hold_.Reset();
  }
  // A sidebar that just came on screen: where is the pointer?
  if ((to == PeekState::kShowing || to == PeekState::kShown) &&
      from == PeekState::kArming) {
    // The pointer is in the hot zone; the footprint test starts now.
    controller_.SetPointerOverSidebar(
        FootprintInScreen().Contains(CursorPoint()));
  }
  UpdateCursorPoll();
}

void AerieSidebarPeekDriver::FinishHidden() {
  motion_.Abort();
  views::View* region = Region();
  if (region && region->layer()) {
    region->layer()->SetTransform(gfx::Transform());
    region->layer()->SetOpacity(1.f);
  }
  view_->SetVisible(false);
  SetFloatingWidth(0);
}

void AerieSidebarPeekDriver::OnF6() {
  if (!active_) {
    return;
  }
  ApplyTimings();
  f6_open_ = true;
  f6_focus_entered_ = false;
  if (!focus_hold_.holds()) {
    focus_hold_ = controller_.AcquireBusy(aerie_sidebar::PeekBusy::kFocus);
  }
  controller_.OnF6();
  // The focus is moved right after this, in the same call. If it did not land
  // in the sidebar by the end of the turn, nothing is left to release the hold.
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&AerieSidebarPeekDriver::CheckFocusEntered,
                                weak_factory_.GetWeakPtr()));
}

void AerieSidebarPeekDriver::CheckFocusEntered() {
  if (f6_open_ && !f6_focus_entered_ && !FocusInsideSidebar()) {
    f6_open_ = false;
    focus_hold_.Reset();
  }
}

bool AerieSidebarPeekDriver::ToggleKeepOpen() {
  if (!active_) {
    return false;
  }
  ApplyTimings();
  controller_.ToggleKeepOpen();
  view_->ShowPeekToast(controller_.keep_open());
  return true;
}

bool AerieSidebarPeekDriver::FocusInsideSidebar() const {
  views::FocusManager* fm = view_->GetFocusManager();
  views::View* focused = fm ? fm->GetFocusedView() : nullptr;
  return focused && view_->Contains(focused);
}

void AerieSidebarPeekDriver::OnDidChangeFocus(views::View* focused_before,
                                              views::View* focused_now) {
  if (f6_open_) {
    if (focused_now && view_->Contains(focused_now)) {
      f6_focus_entered_ = true;
    } else if (f6_focus_entered_) {
      // F6 took the focus back to the page: the sidebar may go.
      f6_open_ = false;
      f6_focus_entered_ = false;
      focus_hold_.Reset();
    }
  }
  // The omnibox with focus holds the sidebar for as long as it has it.
  if (view_->omnibox_has_focus()) {
    if (!omnibox_hold_.holds()) {
      omnibox_hold_ = controller_.AcquireBusy(aerie_sidebar::PeekBusy::kOmnibox);
    }
  } else {
    omnibox_hold_.Reset();
  }
  HoldForBubble();
}

void AerieSidebarPeekDriver::HoldForBubble() {
  views::Widget* parent = view_->GetWidget();
  if (!parent) {
    return;
  }
  // A bubble is a transient child of the window's native view, not a child
  // window of it.
  for (views::Widget* child :
       views::Widget::GetAllOwnedWidgets(parent->GetNativeView())) {
    if (child == parent || child->IsClosed() || !child->IsVisible()) {
      continue;
    }
    views::WidgetDelegate* delegate = child->widget_delegate();
    views::BubbleDialogDelegate* bubble =
        delegate ? delegate->AsBubbleDialogDelegate() : nullptr;
    if (bubble && bubble->GetAnchorView() &&
        view_->Contains(bubble->GetAnchorView())) {
      if (tracked_bubble_ != child) {
        bubble_observation_.Reset();
        tracked_bubble_ = child;
        bubble_observation_.Observe(child);
      }
      if (!bubble_hold_.holds()) {
        bubble_hold_ =
            controller_.AcquireBusy(aerie_sidebar::PeekBusy::kGuardBubble);
      }
      return;
    }
  }
}

void AerieSidebarPeekDriver::OnWidgetDestroying(views::Widget* widget) {
  if (widget == tracked_bubble_) {
    bubble_observation_.Reset();
    tracked_bubble_ = nullptr;
    // The bubble that kept the sidebar open is gone: the machine looks again.
    bubble_hold_.Reset();
  } else if (widget == view_->GetWidget()) {
    widget_observation_.Reset();
  }
}
