// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_PEEK_DRIVER_H_
#define CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_PEEK_DRIVER_H_

#include <array>
#include <memory>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/timer/timer.h"
#include "components/aerie_design_tokens/aerie_motion.h"
#include "components/aerie_sidebar/aerie_sidebar_peek.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/events/event_observer.h"
#include "ui/views/event_monitor.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_observer.h"

class AerieSidebarView;

namespace views {
class View;
}  // namespace views

// Runs the auto-hide peek of one window (issue #184): it feeds the pointer, the
// focus and the busy sources of the window to a SidebarPeekController, and
// does what the controller asks: slides the sidebar's region in and out over
// the page. The controller and this object are made with the sidebar view and
// do nothing until the sidebar is auto-hidden: the pointer monitor is made
// then, the cursor poll runs only while the pointer is outside the window with
// the sidebar on screen, and an idle window has no timer of this slice.
class AerieSidebarPeekDriver
    : public aerie_sidebar::SidebarPeekController::Observer,
      public ui::EventObserver,
      public ui::AcceleratorTarget,
      public views::FocusChangeListener,
      public views::WidgetObserver {
 public:
  explicit AerieSidebarPeekDriver(AerieSidebarView* view);
  AerieSidebarPeekDriver(const AerieSidebarPeekDriver&) = delete;
  AerieSidebarPeekDriver& operator=(const AerieSidebarPeekDriver&) = delete;
  ~AerieSidebarPeekDriver() override;

  // Auto-hide is on and the sidebar is undocked: the sidebar is out of the
  // layout and peeks. Off: it is docked or not auto-hidden and the driver does
  // nothing. Safe to call with the state it already has.
  void SetActive(bool active);
  bool active() const { return active_; }

  aerie_sidebar::SidebarPeekController& controller() { return controller_; }
  const aerie_sidebar::SidebarPeekController& controller() const {
    return controller_;
  }

  // F6 and Ctrl+Alt+S (issue #184). OnF6 opens a hidden sidebar and holds it
  // open while focus is inside. ToggleKeepOpen returns true if it handled the
  // key (the driver is active).
  void OnF6();
  bool ToggleKeepOpen();

  // Looks at every busy source again and holds or releases `ScopedBusy` for it.
  // The sources call this when they start and when they end.
  void SyncBusy();
  // A resize drag of the sidebar's edge runs (set by the region view).
  void SetResizing(bool resizing);

  // For tests: the pointer is at `screen_point`, as the monitor would tell.
  void PointerMovedForTesting(const gfx::Point& screen_point) {
    HandlePointer(screen_point);
  }
  void PointerExitedForTesting() { HandlePointerExit(); }
  bool monitor_for_testing() const { return !!monitor_; }
  bool poll_running_for_testing() const { return poll_timer_.IsRunning(); }
  // The rect (in screen px) that counts as "over the sidebar" when it is on
  // screen, and the hot zone along the edge.
  gfx::Rect FootprintInScreen() const;
  gfx::Rect HotZoneInScreen() const;

  // aerie_sidebar::SidebarPeekController::Observer:
  void OnPeekShow() override;
  void OnPeekHide() override;
  void OnPeekStateChanged(aerie_sidebar::PeekState from,
                          aerie_sidebar::PeekState to) override;
  // ui::AcceleratorTarget: Ctrl+Alt+S.
  bool AcceleratorPressed(const ui::Accelerator& accelerator) override {
    return ToggleKeepOpen();
  }
  bool CanHandleAccelerators() const override { return active_; }
  // ui::EventObserver:
  void OnEvent(const ui::Event& event) override;
  // views::FocusChangeListener:
  void OnWillChangeFocus(views::View* focused_before,
                         views::View* focused_now) override {}
  void OnDidChangeFocus(views::View* focused_before,
                        views::View* focused_now) override;
  // views::WidgetObserver:
  void OnWidgetDestroying(views::Widget* widget) override;

 private:
  // The view that holds the sidebar in the window's layout.
  views::View* Region() const;
  views::View* Page() const;
  int OuterSign() const;
  gfx::Rect WindowInScreen() const;
  void HandlePointer(const gfx::Point& screen_point);
  void HandlePointerExit();
  void UpdateCursorPoll();
  void SetFloatingWidth(int width);
  void SlideTo(bool show);
  void FinishHidden();
  bool FocusInsideSidebar() const;
  bool BubbleOpenInSidebar();
  void WantBusy(aerie_sidebar::PeekBusy source, bool want);
  void ApplyTimings();

  const raw_ptr<AerieSidebarView> view_;
  aerie_sidebar::SidebarPeekController controller_;
  bool active_ = false;
  std::unique_ptr<views::EventMonitor> monitor_;
  base::RepeatingTimer poll_timer_;
  aerie::motion::MotionHandle motion_;
  raw_ptr<views::FocusManager> focus_manager_ = nullptr;
  bool resizing_ = false;
  bool f6_open_ = false;
  bool f6_focus_entered_ = false;
  std::array<aerie_sidebar::ScopedBusy,
             static_cast<size_t>(aerie_sidebar::PeekBusy::kCount)>
      busy_;
  raw_ptr<views::Widget> tracked_bubble_ = nullptr;
  base::WeakPtrFactory<AerieSidebarPeekDriver> weak_factory_{this};
  base::ScopedObservation<views::Widget, views::WidgetObserver>
      bubble_observation_{this};
};

#endif  // CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_PEEK_DRIVER_H_
