// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_SIDEBAR_AERIE_SIDEBAR_PEEK_H_
#define COMPONENTS_AERIE_SIDEBAR_AERIE_SIDEBAR_PEEK_H_

#include <array>
#include <memory>
#include <utility>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "base/timer/timer.h"

// The auto-hide peek (issue #184, docs/sidebar-design.md, "Auto-hide peek"): the
// state machine that decides when a hidden sidebar slides in and out. It has no
// views. It owns exactly one timer, cancelled on every transition, so nothing
// can be left pending and a state cannot be stuck. The view feeds it what the
// pointer and the keyboard do, and does what Show() and Hide() ask.
namespace aerie_sidebar {

enum class PeekState {
  kHidden,
  kArming,
  kShowing,
  kShown,
  kPinned,
  kLeaving,
  kHiding,
};

const char* PeekStateName(PeekState state);

// Everything that keeps the sidebar open. A source holds a ScopedBusy for as
// long as it is active; releasing it (even by destroying the holder) makes the
// machine look again.
enum class PeekBusy {
  kMenu,
  kPopover,
  kGuardBubble,
  kFolderSheet,
  kDrag,
  kRename,
  kOmnibox,
  kResize,
  // F6 moved focus into the sidebar and it is still there.
  kFocus,
  kCount,
};

class SidebarPeekController;

// A hold on the sidebar. Move-only; the destructor releases it. A hold that
// outlives the controller is harmless.
class ScopedBusy {
 public:
  ScopedBusy() = default;
  ScopedBusy(ScopedBusy&& other) noexcept;
  ScopedBusy& operator=(ScopedBusy&& other) noexcept;
  ScopedBusy(const ScopedBusy&) = delete;
  ScopedBusy& operator=(const ScopedBusy&) = delete;
  ~ScopedBusy();

  bool holds() const { return !!controller_; }
  // Releases early.
  void Reset();

 private:
  friend class SidebarPeekController;
  ScopedBusy(base::WeakPtr<SidebarPeekController> controller, PeekBusy source)
      : controller_(std::move(controller)), source_(source) {}

  base::WeakPtr<SidebarPeekController> controller_;
  PeekBusy source_ = PeekBusy::kMenu;
};

// The timings, from the motion tokens. The dwell, the keep-open time and the
// grace distances do not change under reduced motion; the slides become a short
// fade.
struct PeekTimings {
  base::TimeDelta delay;
  base::TimeDelta leave;
  base::TimeDelta show;
  base::TimeDelta hide;
  int grace_x = 0;
  int grace_y = 0;
};

PeekTimings PeekTimingsFor(bool reduced_motion);

class SidebarPeekController {
 public:
  class Observer : public base::CheckedObserver {
   public:
    // Slide the sidebar in, from where it is now (a hide that is still sliding
    // out reverses from its current offset).
    virtual void OnPeekShow() {}
    // Slide the sidebar out.
    virtual void OnPeekHide() {}
    // Every change of state, after Show() and Hide().
    virtual void OnPeekStateChanged(PeekState from, PeekState to) {}
  };

  // `runner` runs the one timer.
  SidebarPeekController(scoped_refptr<base::SequencedTaskRunner> runner,
                        PeekTimings timings);
  SidebarPeekController(const SidebarPeekController&) = delete;
  SidebarPeekController& operator=(const SidebarPeekController&) = delete;
  ~SidebarPeekController();

  void AddObserver(Observer* observer) { observers_.AddObserver(observer); }
  void RemoveObserver(Observer* observer) {
    observers_.RemoveObserver(observer);
  }

  PeekState state() const { return state_; }
  // The sidebar is on screen (or sliding in or out).
  bool visible() const { return state_ != PeekState::kHidden && state_ != PeekState::kArming; }
  bool timer_pending() const { return timer_.IsRunning(); }
  void SetTimings(PeekTimings timings) { timings_ = timings; }
  const PeekTimings& timings() const { return timings_; }

  // The pointer is in the hot zone along the window's edge.
  void SetPointerInHotZone(bool inside);
  // The pointer is over the sidebar's layout footprint (never the moving
  // element: a pointer test against something that slides makes the edge
  // shake).
  void SetPointerOverSidebar(bool over);
  // The pointer is out of the window. `dx` and `dy` are its distances from the
  // sidebar in px (0 when level with it). The grace distance applies only here.
  void SetPointerOutsideWindow(int dx, int dy);
  // The pointer is back in the window.
  void SetPointerInsideWindow();

  // F6: opens a hidden sidebar (the view then moves focus into it and holds
  // PeekBusy::kFocus while it stays there).
  void OnF6();
  // Ctrl+Alt+S: keeps the peek open, or lets it go again.
  void ToggleKeepOpen();
  bool keep_open() const { return keep_open_; }
  // Ctrl+S, docking or undocking: back to hidden, every flag cleared. Holds of
  // busy sources stay, because their holders own them.
  void Reset();

  // A hold on `source`. The sidebar stays while any hold exists.
  ScopedBusy AcquireBusy(PeekBusy source);
  bool busy() const;
  bool busy(PeekBusy source) const {
    return busy_counts_[static_cast<size_t>(source)] > 0;
  }

  // True while the view should poll the screen cursor: the sidebar is on
  // screen and the pointer is out of the window. Off in every other state.
  bool WantsCursorPoll() const;

 private:
  friend class ScopedBusy;

  void Release(PeekBusy source);
  // True when the pointer is neither near the sidebar nor within the grace.
  bool PointerAway() const;
  void Evaluate();
  // While observers are being told of a change, an input that would start
  // another one waits: true when `input` was queued (it runs after the loops).
  bool Defer(void (SidebarPeekController::*input)());
  void Enter(PeekState next, base::TimeDelta timer);
  void OnTimer();

  PeekTimings timings_;
  base::OneShotTimer timer_;
  base::ObserverList<Observer> observers_;
  PeekState state_ = PeekState::kHidden;
  bool notifying_ = false;
  bool reevaluate_ = false;
  std::vector<base::OnceClosure> deferred_;
  bool in_hot_zone_ = false;
  bool over_sidebar_ = false;
  bool outside_window_ = false;
  int outside_dx_ = 0;
  int outside_dy_ = 0;
  bool keep_open_ = false;
  std::array<int, static_cast<size_t>(PeekBusy::kCount)> busy_counts_{};
  base::WeakPtrFactory<SidebarPeekController> weak_factory_{this};
};

}  // namespace aerie_sidebar

#endif  // COMPONENTS_AERIE_SIDEBAR_AERIE_SIDEBAR_PEEK_H_
