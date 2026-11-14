// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_PEEK_DRIVER_H_
#define CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_PEEK_DRIVER_H_

#include <memory>
#include <optional>

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

  // Reads the reduced-motion state again and gives the controller its timings.
  // Every input to the controller is preceded by this, so that a change of the
  // pref or of the OS setting is in force for the next peek.
  void ApplyTimings();
  // Where the screen cursor is, as the monitor would have told. Called when
  // the window loses activation (no more events come then, and the cursor can
  // leave without one) and by the poll.
  void RecheckPointer();

  // For tests: the pointer is at `screen_point`, as the monitor would tell.
  void PointerMovedForTesting(const gfx::Point& screen_point) {
    HandlePointer(screen_point);
  }
  void PointerExitedForTesting() { HandlePointerExit(); }
  // The cursor, in place of the screen's (a test cannot move the real one
  // without a pointer event, which the monitor would see).
  void SetCursorForTesting(std::optional<gfx::Point> point) {
    cursor_for_testing_ = point;
  }
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
  void OnWidgetActivationChanged(views::Widget* widget, bool active) override;

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
  // Holds the peek open for a bubble anchored in the sidebar (the Guard panel
  // is made by the page-action code, so the sidebar finds it here) until it
  // is destroyed.
  void HoldForBubble();
  gfx::Point CursorPoint() const;
  void CheckFocusEntered();

  const raw_ptr<AerieSidebarView> view_;
  aerie_sidebar::SidebarPeekController controller_;
  bool active_ = false;
  std::unique_ptr<views::EventMonitor> monitor_;
  base::RepeatingTimer poll_timer_;
  aerie::motion::MotionHandle motion_;
  raw_ptr<views::FocusManager> focus_manager_ = nullptr;
  std::optional<gfx::Point> cursor_for_testing_;
  bool f6_open_ = false;
  bool f6_focus_entered_ = false;
  // The sources this driver owns: focus (F6 and the omnibox) and the bubble.
  // The sidebar's own sources hold theirs in the view.
  aerie_sidebar::ScopedBusy focus_hold_;
  aerie_sidebar::ScopedBusy omnibox_hold_;
  aerie_sidebar::ScopedBusy bubble_hold_;
  raw_ptr<views::Widget> tracked_bubble_ = nullptr;
  base::WeakPtrFactory<AerieSidebarPeekDriver> weak_factory_{this};
  base::ScopedObservation<views::Widget, views::WidgetObserver>
      bubble_observation_{this};
  base::ScopedObservation<views::Widget, views::WidgetObserver>
      widget_observation_{this};
};

#endif  // CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_PEEK_DRIVER_H_
