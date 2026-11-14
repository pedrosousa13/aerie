// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_peek_driver.h"

#include <algorithm>

#include "base/task/single_thread_task_runner.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_view.h"
#include "components/aerie_design_tokens/aerie_tokens.h"
#include "components/aerie_sidebar/aerie_sidebar_density.h"
#include "components/aerie_sidebar/aerie_sidebar_floating.h"
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

// The room between the floating sidebar and the window edge. Its footprint
// includes it, so the pointer crossing the gap is still over the sidebar. This
// is the layout's inset and `size-inset-comfortable`.
constexpr int kFloatInset = aerie::tokens::kSizeInsetComfortable;

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
  for (aerie_sidebar::ScopedBusy& busy : busy_) {
    busy.Reset();
  }
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

gfx::Rect AerieSidebarPeekDriver::FootprintInScreen() const {
  gfx::Rect window = WindowInScreen();
  const int width = view_->column_width() + kFloatInset;
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
    controller_.Reset();
    if (widget) {
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
  for (aerie_sidebar::ScopedBusy& busy : busy_) {
    busy.Reset();
  }
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
  const gfx::Rect window = WindowInScreen();
  if (!window.Contains(screen_point)) {
    // Outside the window: the distance to the sidebar decides.
    const gfx::Rect footprint = FootprintInScreen();
    const int dx = std::max({0, footprint.x() - screen_point.x(),
                             screen_point.x() - footprint.right()});
    const int dy = std::max({0, footprint.y() - screen_point.y(),
                             screen_point.y() - footprint.bottom()});
    SyncBusy();
    controller_.SetPointerOutsideWindow(dx, dy);
    UpdateCursorPoll();
    return;
  }
  const bool visible = controller_.visible();
  const bool over = visible && FootprintInScreen().Contains(screen_point);
  const bool hot = HotZoneInScreen().Contains(screen_point);
  if (!over && !hot) {
    // About to leave: a bubble open on the sidebar (the Guard panel) counts.
    SyncBusy();
  }
  controller_.SetPointerInsideWindow();
  controller_.SetPointerOverSidebar(over);
  controller_.SetPointerInHotZone(hot);
  UpdateCursorPoll();
}

void AerieSidebarPeekDriver::HandlePointerExit() {
  // The screen cursor says where it went; a web page would only see the exit
  // point.
  HandlePointer(display::Screen::Get()->GetCursorScreenPoint());
}

void AerieSidebarPeekDriver::UpdateCursorPoll() {
  if (controller_.WantsCursorPoll()) {
    if (!poll_timer_.IsRunning()) {
      poll_timer_.Start(FROM_HERE, kCursorPollInterval,
                        base::BindRepeating(
                            [](AerieSidebarPeekDriver* self) {
                              self->HandlePointer(
                                  display::Screen::Get()->GetCursorScreenPoint());
                            },
                            base::Unretained(this)));
    }
  } else {
    poll_timer_.Stop();
  }
}

void AerieSidebarPeekDriver::OnPeekShow() {
  ApplyTimings();
  SlideTo(/*show=*/true);
}

void AerieSidebarPeekDriver::OnPeekHide() {
  ApplyTimings();
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
    SyncBusy();
  }
  // A sidebar that just came on screen: where is the pointer?
  if ((to == PeekState::kShowing || to == PeekState::kShown) &&
      from == PeekState::kArming) {
    // The pointer is in the hot zone; the footprint test starts now.
    controller_.SetPointerOverSidebar(
        FootprintInScreen().Contains(
            display::Screen::Get()->GetCursorScreenPoint()));
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
  f6_open_ = true;
  f6_focus_entered_ = false;
  controller_.OnF6();
  SyncBusy();
}

bool AerieSidebarPeekDriver::ToggleKeepOpen() {
  if (!active_) {
    return false;
  }
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
    }
  }
  SyncBusy();
}

bool AerieSidebarPeekDriver::BubbleOpenInSidebar() {
  views::Widget* parent = view_->GetWidget();
  if (!parent) {
    return false;
  }
  for (views::Widget* child :
       views::Widget::GetAllChildWidgets(parent->GetNativeView())) {
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
      return true;
    }
  }
  return false;
}

void AerieSidebarPeekDriver::OnWidgetDestroying(views::Widget* widget) {
  if (widget == tracked_bubble_) {
    bubble_observation_.Reset();
    tracked_bubble_ = nullptr;
    // The bubble that kept the sidebar open is gone: look again, after it is.
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&AerieSidebarPeekDriver::SyncBusy,
                                  weak_factory_.GetWeakPtr()));
  }
}

void AerieSidebarPeekDriver::SetResizing(bool resizing) {
  resizing_ = resizing;
  SyncBusy();
}

void AerieSidebarPeekDriver::WantBusy(aerie_sidebar::PeekBusy source,
                                      bool want) {
  aerie_sidebar::ScopedBusy& hold = busy_[static_cast<size_t>(source)];
  if (want && !hold.holds()) {
    hold = controller_.AcquireBusy(source);
  } else if (!want && hold.holds()) {
    hold.Reset();
  }
}

void AerieSidebarPeekDriver::SyncBusy() {
  using aerie_sidebar::PeekBusy;
  if (!active_) {
    return;
  }
  const bool shown = controller_.visible();
  WantBusy(PeekBusy::kMenu, shown && view_->context_menu_open());
  WantBusy(PeekBusy::kPopover, shown && view_->rename_popover_open());
  WantBusy(PeekBusy::kFolderSheet, shown && !!view_->folder_sheet());
  WantBusy(PeekBusy::kDrag, shown && view_->dragging());
  WantBusy(PeekBusy::kRename, shown && view_->any_row_renaming());
  WantBusy(PeekBusy::kOmnibox, shown && view_->omnibox_has_focus());
  WantBusy(PeekBusy::kResize, shown && resizing_);
  WantBusy(PeekBusy::kFocus, shown && f6_open_ &&
                                  (!f6_focus_entered_ || FocusInsideSidebar()));
  WantBusy(PeekBusy::kGuardBubble, shown && BubbleOpenInSidebar());
}
