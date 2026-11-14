// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_sidebar/aerie_sidebar_peek.h"

#include <utility>

#include "base/check.h"
#include "components/aerie_design_tokens/aerie_tokens.h"

namespace aerie_sidebar {

const char* PeekStateName(PeekState state) {
  switch (state) {
    case PeekState::kHidden:
      return "hidden";
    case PeekState::kArming:
      return "arming";
    case PeekState::kShowing:
      return "showing";
    case PeekState::kShown:
      return "shown";
    case PeekState::kPinned:
      return "pinned";
    case PeekState::kLeaving:
      return "leaving";
    case PeekState::kHiding:
      return "hiding";
  }
  return "";
}

PeekTimings PeekTimingsFor(bool reduced) {
  namespace t = aerie::tokens;
  PeekTimings timings;
  timings.delay = base::Milliseconds(
      reduced ? t::kMotionTimingPeekDelayReduced : t::kMotionTimingPeekDelay);
  timings.leave = base::Milliseconds(
      reduced ? t::kMotionTimingPeekLeaveReduced : t::kMotionTimingPeekLeave);
  timings.show = base::Milliseconds(
      t::GetDurationMs(t::MotionDuration::kPeekShow, reduced));
  timings.hide = base::Milliseconds(
      t::GetDurationMs(t::MotionDuration::kPeekHide, reduced));
  timings.grace_x = reduced ? t::kMotionDistancePeekGraceXReduced
                            : t::kMotionDistancePeekGraceX;
  timings.grace_y = reduced ? t::kMotionDistancePeekGraceYReduced
                            : t::kMotionDistancePeekGraceY;
  return timings;
}

ScopedBusy::ScopedBusy(ScopedBusy&& other) noexcept
    : controller_(std::move(other.controller_)), source_(other.source_) {
  other.controller_.reset();
}

ScopedBusy& ScopedBusy::operator=(ScopedBusy&& other) noexcept {
  if (this != &other) {
    Reset();
    controller_ = std::move(other.controller_);
    source_ = other.source_;
    other.controller_.reset();
  }
  return *this;
}

ScopedBusy::~ScopedBusy() {
  Reset();
}

void ScopedBusy::Reset() {
  if (controller_) {
    // Take the pointer first: Release() can run an observer that destroys
    // this hold's owner.
    base::WeakPtr<SidebarPeekController> controller = std::move(controller_);
    controller_.reset();
    controller->Release(source_);
  }
}

SidebarPeekController::SidebarPeekController(
    scoped_refptr<base::SequencedTaskRunner> runner,
    PeekTimings timings)
    : runner_(std::move(runner)), timings_(timings) {
  timer_.SetTaskRunner(runner_);
}

SidebarPeekController::~SidebarPeekController() = default;

void SidebarPeekController::SetPointerInHotZone(bool inside) {
  in_hot_zone_ = inside;
  Evaluate();
}

void SidebarPeekController::SetPointerOverSidebar(bool over) {
  over_sidebar_ = over;
  Evaluate();
}

void SidebarPeekController::SetPointerOutsideWindow(int dx, int dy) {
  // A pointer out of the window is over nothing in it.
  in_hot_zone_ = false;
  over_sidebar_ = false;
  outside_window_ = true;
  outside_dx_ = dx;
  outside_dy_ = dy;
  Evaluate();
}

void SidebarPeekController::SetPointerInsideWindow() {
  outside_window_ = false;
  Evaluate();
}

void SidebarPeekController::OnF6() {
  switch (state_) {
    case PeekState::kHidden:
    case PeekState::kArming:
      Enter(PeekState::kShowing, timings_.show);
      break;
    case PeekState::kHiding:
      Enter(PeekState::kShowing, timings_.show);
      break;
    case PeekState::kLeaving:
      Enter(PeekState::kShown, base::TimeDelta());
      break;
    case PeekState::kShowing:
    case PeekState::kShown:
    case PeekState::kPinned:
      break;
  }
}

void SidebarPeekController::ToggleKeepOpen() {
  keep_open_ = !keep_open_;
  switch (state_) {
    case PeekState::kHidden:
    case PeekState::kArming:
    case PeekState::kHiding:
      // Opening with Ctrl+Alt+S: it settles as pinned. Closing it again while
      // hidden does not open anything.
      if (keep_open_) {
        Enter(PeekState::kShowing, timings_.show);
      }
      break;
    case PeekState::kShowing:
      // Settles as pinned or shown, whichever the flag says then.
      break;
    case PeekState::kShown:
    case PeekState::kLeaving:
      if (keep_open_) {
        Enter(PeekState::kPinned, base::TimeDelta());
      }
      break;
    case PeekState::kPinned:
      if (!keep_open_) {
        Enter(PeekState::kShown, base::TimeDelta());
        Evaluate();
      }
      break;
  }
}

void SidebarPeekController::Reset() {
  keep_open_ = false;
  in_hot_zone_ = false;
  over_sidebar_ = false;
  outside_window_ = false;
  timer_.Stop();
  if (state_ != PeekState::kHidden) {
    Enter(PeekState::kHidden, base::TimeDelta());
  }
}

ScopedBusy SidebarPeekController::AcquireBusy(PeekBusy source) {
  CHECK_LT(static_cast<size_t>(source), busy_counts_.size());
  ++busy_counts_[static_cast<size_t>(source)];
  return ScopedBusy(weak_factory_.GetWeakPtr(), source);
}

void SidebarPeekController::Release(PeekBusy source) {
  int& count = busy_counts_[static_cast<size_t>(source)];
  CHECK_GT(count, 0);
  --count;
  // Every busy state ends with a look at where the pointer is.
  Evaluate();
}

bool SidebarPeekController::busy() const {
  for (int count : busy_counts_) {
    if (count > 0) {
      return true;
    }
  }
  return false;
}

bool SidebarPeekController::WantsCursorPoll() const {
  if (!outside_window_) {
    return false;
  }
  switch (state_) {
    case PeekState::kShowing:
    case PeekState::kShown:
    case PeekState::kPinned:
    case PeekState::kLeaving:
      return true;
    case PeekState::kHidden:
    case PeekState::kArming:
    case PeekState::kHiding:
      return false;
  }
  return false;
}

bool SidebarPeekController::PointerAway() const {
  if (in_hot_zone_ || over_sidebar_) {
    return false;
  }
  if (outside_window_) {
    return outside_dx_ > timings_.grace_x || outside_dy_ > timings_.grace_y;
  }
  return true;
}

void SidebarPeekController::Enter(PeekState next, base::TimeDelta timer) {
  const PeekState from = state_;
  timer_.Stop();
  state_ = next;
  if (!timer.is_zero()) {
    timer_.Start(FROM_HERE, timer,
                 base::BindOnce(&SidebarPeekController::OnTimer,
                                base::Unretained(this)));
  }
  if (next == PeekState::kShowing) {
    for (Observer& observer : observers_) {
      observer.OnPeekShow();
    }
  } else if (next == PeekState::kHiding) {
    for (Observer& observer : observers_) {
      observer.OnPeekHide();
    }
  }
  if (from != next) {
    for (Observer& observer : observers_) {
      observer.OnPeekStateChanged(from, next);
    }
  }
}

void SidebarPeekController::Evaluate() {
  switch (state_) {
    case PeekState::kHidden:
      if (in_hot_zone_) {
        Enter(PeekState::kArming, timings_.delay);
      }
      break;
    case PeekState::kArming:
      if (!in_hot_zone_) {
        Enter(PeekState::kHidden, base::TimeDelta());
      }
      break;
    case PeekState::kShowing:
      // A leave during the slide-in waits for it to settle.
      break;
    case PeekState::kShown:
      if (PointerAway() && !busy()) {
        Enter(PeekState::kLeaving, timings_.leave);
      }
      break;
    case PeekState::kPinned:
      break;
    case PeekState::kLeaving:
      if (!PointerAway()) {
        Enter(PeekState::kShown, base::TimeDelta());
      }
      break;
    case PeekState::kHiding:
      if (!PointerAway()) {
        // Reverses from the current offset: the view starts the slide from
        // where the layer is.
        Enter(PeekState::kShowing, timings_.show);
      }
      break;
  }
}

void SidebarPeekController::OnTimer() {
  switch (state_) {
    case PeekState::kArming:
      if (in_hot_zone_) {
        Enter(PeekState::kShowing, timings_.show);
      } else {
        Enter(PeekState::kHidden, base::TimeDelta());
      }
      break;
    case PeekState::kShowing:
      Enter(keep_open_ ? PeekState::kPinned : PeekState::kShown,
            base::TimeDelta());
      // The pointer may have left while it slid in.
      Evaluate();
      break;
    case PeekState::kLeaving:
      if (PointerAway() && !busy()) {
        Enter(PeekState::kHiding, timings_.hide);
      } else {
        Enter(PeekState::kShown, base::TimeDelta());
        Evaluate();
      }
      break;
    case PeekState::kHiding:
      Enter(PeekState::kHidden, base::TimeDelta());
      Evaluate();
      break;
    case PeekState::kHidden:
    case PeekState::kShown:
    case PeekState::kPinned:
      NOTREACHED();
  }
}

}  // namespace aerie_sidebar
