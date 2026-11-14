// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_sidebar/aerie_sidebar_peek.h"

#include <algorithm>
#include <string>
#include <vector>

#include "base/test/task_environment.h"
#include "components/aerie_design_tokens/aerie_tokens.h"
#include "testing/gtest/include/gtest/gtest.h"

// Every row of the transition table in docs/sidebar-design.md ("Auto-hide
// peek"), the busy table, the grace distance, and one test for each known Zen
// bug (issue #184). Time is mocked, so a test moves it by the token values.
namespace aerie_sidebar {
namespace {

using S = PeekState;

constexpr base::TimeDelta kDelay = base::Milliseconds(150);
constexpr base::TimeDelta kLeave = base::Milliseconds(150);
constexpr base::TimeDelta kShow = base::Milliseconds(250);
constexpr base::TimeDelta kHide = base::Milliseconds(150);
constexpr base::TimeDelta kEdge = base::Milliseconds(1);

class Recorder : public SidebarPeekController::Observer {
 public:
  void OnPeekShow() override { log.push_back("show"); }
  void OnPeekHide() override { log.push_back("hide"); }
  void OnPeekStateChanged(PeekState from, PeekState to) override {
    log.push_back(std::string(PeekStateName(from)) + ">" + PeekStateName(to));
  }
  std::vector<std::string> log;
};

class PeekTest : public testing::Test {
 public:
  PeekTest()
      : controller_(task_environment_.GetMainThreadTaskRunner(),
                    PeekTimingsFor(false)) {
    controller_.AddObserver(&recorder_);
  }
  ~PeekTest() override { controller_.RemoveObserver(&recorder_); }

 protected:
  void Advance(base::TimeDelta delta) { task_environment_.FastForwardBy(delta); }

  // Hidden to shown, pointer parked on the sidebar.
  void OpenByHover() {
    controller_.SetPointerInHotZone(true);
    Advance(kDelay);
    ASSERT_EQ(controller_.state(), S::kShowing);
    controller_.SetPointerOverSidebar(true);
    controller_.SetPointerInHotZone(false);
    Advance(kShow);
    ASSERT_EQ(controller_.state(), S::kShown);
  }

  // The invariant of the slice: at most one timer, and only in the states that
  // wait for something.
  void ExpectTimerMatchesState() {
    EXPECT_LE(task_environment_.GetPendingMainThreadTaskCount(), 1u);
    switch (controller_.state()) {
      case S::kHidden:
      case S::kShown:
      case S::kPinned:
        EXPECT_FALSE(controller_.timer_pending())
            << PeekStateName(controller_.state());
        break;
      case S::kArming:
      case S::kShowing:
      case S::kLeaving:
      case S::kHiding:
        EXPECT_TRUE(controller_.timer_pending())
            << PeekStateName(controller_.state());
        break;
    }
  }

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  SidebarPeekController controller_;
  Recorder recorder_;
};

TEST_F(PeekTest, TheTimingsAreTheTokens) {
  EXPECT_EQ(kDelay.InMilliseconds(), aerie::tokens::kMotionTimingPeekDelay);
  EXPECT_EQ(kLeave.InMilliseconds(), aerie::tokens::kMotionTimingPeekLeave);
  EXPECT_EQ(kShow.InMilliseconds(), aerie::tokens::kMotionDurationPeekShow);
  EXPECT_EQ(kHide.InMilliseconds(), aerie::tokens::kMotionDurationPeekHide);
  const PeekTimings t = PeekTimingsFor(false);
  EXPECT_EQ(t.delay, kDelay);
  EXPECT_EQ(t.leave, kLeave);
  EXPECT_EQ(t.show, kShow);
  EXPECT_EQ(t.hide, kHide);
  EXPECT_EQ(t.grace_x, 200);
  EXPECT_EQ(t.grace_y, 100);
}

TEST_F(PeekTest, ReducedMotionShortensTheSlidesAndKeepsTheDelaysAndGrace) {
  const PeekTimings normal = PeekTimingsFor(false);
  const PeekTimings reduced = PeekTimingsFor(true);
  EXPECT_EQ(reduced.show, base::Milliseconds(100));
  EXPECT_LT(reduced.hide, normal.hide);
  EXPECT_EQ(reduced.delay, normal.delay);
  EXPECT_EQ(reduced.leave, normal.leave);
  EXPECT_EQ(reduced.grace_x, normal.grace_x);
  EXPECT_EQ(reduced.grace_y, normal.grace_y);
}

// ---- The transition table, one test per row ----

TEST_F(PeekTest, HiddenToArmingWhenThePointerEntersTheHotZone) {
  EXPECT_EQ(controller_.state(), S::kHidden);
  controller_.SetPointerInHotZone(true);
  EXPECT_EQ(controller_.state(), S::kArming);
  ExpectTimerMatchesState();
}

TEST_F(PeekTest, ArmingToShowingAfterTheDwellWhileStillInTheZone) {
  controller_.SetPointerInHotZone(true);
  Advance(kDelay - kEdge);
  EXPECT_EQ(controller_.state(), S::kArming);
  Advance(kEdge);
  EXPECT_EQ(controller_.state(), S::kShowing);
  EXPECT_EQ(recorder_.log.back(), "arming>showing");
  EXPECT_NE(std::find(recorder_.log.begin(), recorder_.log.end(), "show"),
            recorder_.log.end());
  ExpectTimerMatchesState();
}

TEST_F(PeekTest, ArmingToHiddenWhenThePointerLeavesTheZone) {
  controller_.SetPointerInHotZone(true);
  Advance(base::Milliseconds(60));
  controller_.SetPointerInHotZone(false);
  EXPECT_EQ(controller_.state(), S::kHidden);
  ExpectTimerMatchesState();
  Advance(kDelay * 4);
  EXPECT_EQ(controller_.state(), S::kHidden);
}

TEST_F(PeekTest, ArmingToHiddenWhenThePointerLeavesTheWindow) {
  controller_.SetPointerInHotZone(true);
  controller_.SetPointerInHotZone(false);
  controller_.SetPointerOutsideWindow(10, 0);
  EXPECT_EQ(controller_.state(), S::kHidden);
}

TEST_F(PeekTest, HiddenToShowingOnF6) {
  controller_.OnF6();
  EXPECT_EQ(controller_.state(), S::kShowing);
  EXPECT_NE(std::find(recorder_.log.begin(), recorder_.log.end(), "show"),
            recorder_.log.end());
  ExpectTimerMatchesState();
}

TEST_F(PeekTest, HiddenToShowingOnKeepOpenAndSettlesAsPinned) {
  controller_.ToggleKeepOpen();
  EXPECT_EQ(controller_.state(), S::kShowing);
  Advance(kShow);
  EXPECT_EQ(controller_.state(), S::kPinned);
  ExpectTimerMatchesState();
}

TEST_F(PeekTest, ShowingToShownWhenTheSlideSettles) {
  controller_.OnF6();
  controller_.SetPointerOverSidebar(true);
  Advance(kShow - kEdge);
  EXPECT_EQ(controller_.state(), S::kShowing);
  Advance(kEdge);
  EXPECT_EQ(controller_.state(), S::kShown);
  ExpectTimerMatchesState();
}

TEST_F(PeekTest, ShownAndPinnedToggleWithKeepOpen) {
  OpenByHover();
  controller_.ToggleKeepOpen();
  EXPECT_EQ(controller_.state(), S::kPinned);
  EXPECT_TRUE(controller_.keep_open());
  ExpectTimerMatchesState();
  // Pinned stays open with the pointer gone.
  controller_.SetPointerOverSidebar(false);
  Advance(base::Seconds(5));
  EXPECT_EQ(controller_.state(), S::kPinned);
  // Toggling off with the pointer away lets it leave.
  controller_.ToggleKeepOpen();
  EXPECT_FALSE(controller_.keep_open());
  EXPECT_EQ(controller_.state(), S::kLeaving);
  ExpectTimerMatchesState();
}

TEST_F(PeekTest, ShownToLeavingWhenThePointerIsAwayAndNotBusy) {
  OpenByHover();
  controller_.SetPointerOverSidebar(false);
  EXPECT_EQ(controller_.state(), S::kLeaving);
  ExpectTimerMatchesState();
}

TEST_F(PeekTest, LeavingToShownWhenThePointerComesBack) {
  OpenByHover();
  controller_.SetPointerOverSidebar(false);
  Advance(base::Milliseconds(100));
  controller_.SetPointerOverSidebar(true);
  EXPECT_EQ(controller_.state(), S::kShown);
  ExpectTimerMatchesState();
  Advance(base::Seconds(1));
  EXPECT_EQ(controller_.state(), S::kShown);
}

TEST_F(PeekTest, LeavingToHidingAfterTheKeepOpenTime) {
  OpenByHover();
  controller_.SetPointerOverSidebar(false);
  Advance(kLeave - kEdge);
  EXPECT_EQ(controller_.state(), S::kLeaving);
  Advance(kEdge);
  EXPECT_EQ(controller_.state(), S::kHiding);
  EXPECT_EQ(recorder_.log.back(), "leaving>hiding");
  EXPECT_NE(std::find(recorder_.log.begin(), recorder_.log.end(), "hide"),
            recorder_.log.end());
  ExpectTimerMatchesState();
}

TEST_F(PeekTest, LeavingTimerEndsBusyBackToShown) {
  OpenByHover();
  controller_.SetPointerOverSidebar(false);
  ScopedBusy menu = controller_.AcquireBusy(PeekBusy::kMenu);
  Advance(kLeave);
  EXPECT_EQ(controller_.state(), S::kShown);
  ExpectTimerMatchesState();
}

TEST_F(PeekTest, LeavingTimerEndsWithThePointerBackInsideToShown) {
  OpenByHover();
  controller_.SetPointerOverSidebar(false);
  Advance(kLeave - kEdge);
  controller_.SetPointerInHotZone(true);
  EXPECT_EQ(controller_.state(), S::kShown);
}

TEST_F(PeekTest, LeavingTimerEndsWithinTheGraceDistanceToShown) {
  OpenByHover();
  controller_.SetPointerOverSidebar(false);
  // The pointer leaves the window within the grace distance during the keep-open
  // time.
  controller_.SetPointerOutsideWindow(150, 50);
  EXPECT_EQ(controller_.state(), S::kShown);
  ExpectTimerMatchesState();
}

TEST_F(PeekTest, HidingToHiddenWhenTheSlideEnds) {
  OpenByHover();
  controller_.SetPointerOverSidebar(false);
  Advance(kLeave);
  ASSERT_EQ(controller_.state(), S::kHiding);
  Advance(kHide);
  EXPECT_EQ(controller_.state(), S::kHidden);
  ExpectTimerMatchesState();
}

TEST_F(PeekTest, HidingToShowingWhenThePointerReenters) {
  OpenByHover();
  controller_.SetPointerOverSidebar(false);
  Advance(kLeave + base::Milliseconds(50));
  ASSERT_EQ(controller_.state(), S::kHiding);
  controller_.SetPointerInHotZone(true);
  EXPECT_EQ(controller_.state(), S::kShowing);
  ExpectTimerMatchesState();
}

TEST_F(PeekTest, AnyStateToHiddenOnDockAndUndock) {
  for (int target = 0; target < 7; ++target) {
    SCOPED_TRACE(target);
    controller_.Reset();
    switch (target) {
      case 0:  // hidden
        break;
      case 1:  // arming
        controller_.SetPointerInHotZone(true);
        break;
      case 2:  // showing
        controller_.OnF6();
        break;
      case 3:  // shown
        OpenByHover();
        break;
      case 4:  // pinned
        controller_.ToggleKeepOpen();
        Advance(kShow);
        break;
      case 5:  // leaving
        OpenByHover();
        controller_.SetPointerOverSidebar(false);
        break;
      case 6:  // hiding
        OpenByHover();
        controller_.SetPointerOverSidebar(false);
        Advance(kLeave);
        break;
    }
    controller_.Reset();
    EXPECT_EQ(controller_.state(), S::kHidden);
    EXPECT_FALSE(controller_.keep_open());
    ExpectTimerMatchesState();
    Advance(base::Seconds(2));
    EXPECT_EQ(controller_.state(), S::kHidden);
  }
}

// ---- Busy: the sidebar never hides while a source holds ScopedBusy ----

class PeekBusyTest : public PeekTest,
                     public testing::WithParamInterface<PeekBusy> {};

TEST_P(PeekBusyTest, StaysWhileHeldAndHidesAfterTheLastRelease) {
  OpenByHover();
  ScopedBusy hold = controller_.AcquireBusy(GetParam());
  EXPECT_TRUE(controller_.busy());
  controller_.SetPointerOverSidebar(false);
  // Held: no leaving, however long.
  EXPECT_EQ(controller_.state(), S::kShown);
  Advance(base::Seconds(10));
  EXPECT_EQ(controller_.state(), S::kShown);
  ExpectTimerMatchesState();
  // Released: the check happens at once.
  hold.Reset();
  EXPECT_FALSE(controller_.busy());
  EXPECT_EQ(controller_.state(), S::kLeaving);
  Advance(kLeave);
  EXPECT_EQ(controller_.state(), S::kHiding);
  Advance(kHide);
  EXPECT_EQ(controller_.state(), S::kHidden);
}

TEST_P(PeekBusyTest, DestroyingTheHolderReleasesIt) {
  OpenByHover();
  {
    ScopedBusy hold = controller_.AcquireBusy(GetParam());
    controller_.SetPointerOverSidebar(false);
    EXPECT_EQ(controller_.state(), S::kShown);
  }
  EXPECT_EQ(controller_.state(), S::kLeaving);
}

INSTANTIATE_TEST_SUITE_P(
    EverySource,
    PeekBusyTest,
    testing::Values(PeekBusy::kMenu,
                    PeekBusy::kPopover,
                    PeekBusy::kGuardBubble,
                    PeekBusy::kFolderSheet,
                    PeekBusy::kDrag,
                    PeekBusy::kRename,
                    PeekBusy::kOmnibox,
                    PeekBusy::kResize,
                    PeekBusy::kFocus));

TEST_F(PeekTest, TwoHoldsNeedTwoReleases) {
  OpenByHover();
  ScopedBusy a = controller_.AcquireBusy(PeekBusy::kMenu);
  ScopedBusy b = controller_.AcquireBusy(PeekBusy::kMenu);
  ScopedBusy c = controller_.AcquireBusy(PeekBusy::kRename);
  controller_.SetPointerOverSidebar(false);
  a.Reset();
  b.Reset();
  EXPECT_EQ(controller_.state(), S::kShown);
  c.Reset();
  EXPECT_EQ(controller_.state(), S::kLeaving);
}

TEST_F(PeekTest, AHoldCanMoveAndDoubleResetIsHarmless) {
  OpenByHover();
  ScopedBusy a = controller_.AcquireBusy(PeekBusy::kDrag);
  ScopedBusy b = std::move(a);
  EXPECT_FALSE(a.holds());
  EXPECT_TRUE(b.holds());
  b.Reset();
  b.Reset();
  EXPECT_FALSE(controller_.busy(PeekBusy::kDrag));
}

TEST_F(PeekTest, AHoldThatOutlivesTheControllerIsHarmless) {
  ScopedBusy hold;
  {
    SidebarPeekController other(task_environment_.GetMainThreadTaskRunner(),
                                PeekTimingsFor(false));
    hold = other.AcquireBusy(PeekBusy::kMenu);
  }
  hold.Reset();
}

TEST_F(PeekTest, ABusyHoldDuringTheLeavingTimerKeepsItOpenThenLeavesAgain) {
  OpenByHover();
  controller_.SetPointerOverSidebar(false);
  ASSERT_EQ(controller_.state(), S::kLeaving);
  ScopedBusy menu = controller_.AcquireBusy(PeekBusy::kMenu);
  Advance(kLeave);
  EXPECT_EQ(controller_.state(), S::kShown);
  // The leave that happened while busy is remembered and checked again.
  menu.Reset();
  EXPECT_EQ(controller_.state(), S::kLeaving);
}

// ---- Grace distance ----

TEST_F(PeekTest, TheGraceDistanceIs200ByHundredPixels) {
  OpenByHover();
  controller_.SetPointerOverSidebar(false);
  controller_.SetPointerOutsideWindow(200, 100);
  EXPECT_EQ(controller_.state(), S::kShown);
  Advance(base::Seconds(3));
  EXPECT_EQ(controller_.state(), S::kShown);
  controller_.SetPointerOutsideWindow(201, 100);
  EXPECT_EQ(controller_.state(), S::kLeaving);
  controller_.SetPointerOutsideWindow(200, 100);
  EXPECT_EQ(controller_.state(), S::kShown);
  controller_.SetPointerOutsideWindow(200, 101);
  EXPECT_EQ(controller_.state(), S::kLeaving);
}

TEST_F(PeekTest, TheGraceDistanceOnlyAppliesOutsideTheWindow) {
  OpenByHover();
  controller_.SetPointerOutsideWindow(10, 10);
  controller_.SetPointerInsideWindow();
  // Back in the window and not over the sidebar: away, however near.
  controller_.SetPointerOverSidebar(false);
  EXPECT_EQ(controller_.state(), S::kLeaving);
}

TEST_F(PeekTest, TheCursorPollIsWantedOnlyWhileShownAndOutsideTheWindow) {
  EXPECT_FALSE(controller_.WantsCursorPoll());
  OpenByHover();
  EXPECT_FALSE(controller_.WantsCursorPoll());
  controller_.SetPointerOutsideWindow(50, 50);
  EXPECT_TRUE(controller_.WantsCursorPoll());
  controller_.SetPointerInsideWindow();
  EXPECT_FALSE(controller_.WantsCursorPoll());
  controller_.SetPointerOutsideWindow(500, 50);
  EXPECT_EQ(controller_.state(), S::kLeaving);
  EXPECT_TRUE(controller_.WantsCursorPoll());
  Advance(kLeave);
  ASSERT_EQ(controller_.state(), S::kHiding);
  EXPECT_FALSE(controller_.WantsCursorPoll());
  Advance(kHide);
  EXPECT_EQ(controller_.state(), S::kHidden);
  EXPECT_FALSE(controller_.WantsCursorPoll());
}

TEST_F(PeekTest, NoStuckStateWhenThePointerLeavesTheWindowFromTheSidebar) {
  OpenByHover();
  // The pointer goes out of the window far away (a quick flick): the poll's
  // first report is beyond the grace.
  controller_.SetPointerOverSidebar(false);
  controller_.SetPointerOutsideWindow(900, 400);
  Advance(kLeave + kHide);
  EXPECT_EQ(controller_.state(), S::kHidden);
}

// ---- Zen's bugs ----

// Zen #12856: the sidebar shook at the edge. The pointer flips in and out of
// the hot zone every 50 ms; the dwell restarts each time, so it never opens,
// and no state flips twice in a row.
TEST_F(PeekTest, ZenEdgeJitterNeverOpensAndNeverShakes) {
  for (int i = 0; i < 40; ++i) {
    controller_.SetPointerInHotZone(true);
    Advance(base::Milliseconds(50));
    controller_.SetPointerInHotZone(false);
    Advance(base::Milliseconds(50));
    EXPECT_NE(controller_.state(), S::kShowing);
    ExpectTimerMatchesState();
  }
  EXPECT_EQ(controller_.state(), S::kHidden);
  for (const std::string& entry : recorder_.log) {
    EXPECT_TRUE(entry == "hidden>arming" || entry == "arming>hidden") << entry;
  }
}

// Once it is open, jitter between the hot zone and the sidebar footprint (the
// moving element is never tested) does not make it leave and come back.
TEST_F(PeekTest, ZenEdgeJitterOnAShownSidebarChangesNothing) {
  OpenByHover();
  recorder_.log.clear();
  for (int i = 0; i < 40; ++i) {
    controller_.SetPointerOverSidebar(false);
    controller_.SetPointerInHotZone(true);
    Advance(base::Milliseconds(20));
    controller_.SetPointerInHotZone(false);
    controller_.SetPointerOverSidebar(true);
    Advance(base::Milliseconds(20));
  }
  EXPECT_EQ(controller_.state(), S::kShown);
  EXPECT_TRUE(recorder_.log.empty() ||
              std::find(recorder_.log.begin(), recorder_.log.end(), "hide") ==
                  recorder_.log.end());
}

// Zen #8643: stuck open after a menu closed. The menu opens while the pointer
// is over the sidebar, the pointer goes off to the page and the menu closes:
// the release makes the machine look again and it hides.
TEST_F(PeekTest, ZenStuckOpenAfterAMenuClosesIsReleased) {
  OpenByHover();
  ScopedBusy menu = controller_.AcquireBusy(PeekBusy::kMenu);
  controller_.SetPointerOverSidebar(false);
  Advance(base::Seconds(30));
  EXPECT_EQ(controller_.state(), S::kShown);
  menu.Reset();
  Advance(kLeave + kHide);
  EXPECT_EQ(controller_.state(), S::kHidden);
  ExpectTimerMatchesState();
}

// The same bug through the window: the pointer leaves the window while a menu
// is open, far away; the menu closes; it hides without a pointer event.
TEST_F(PeekTest, ZenStuckOpenAfterAMenuClosesWithThePointerOutsideTheWindow) {
  OpenByHover();
  ScopedBusy menu = controller_.AcquireBusy(PeekBusy::kMenu);
  controller_.SetPointerOverSidebar(false);
  controller_.SetPointerOutsideWindow(800, 800);
  Advance(base::Seconds(5));
  menu.Reset();
  Advance(kLeave + kHide);
  EXPECT_EQ(controller_.state(), S::kHidden);
}

// Zen #15995: it retracted too early. The pointer leaves while the sidebar
// slides in. Nothing happens until it settles; then the keep-open time runs.
TEST_F(PeekTest, ZenALeaveDuringTheSlideInWaitsUntilItSettles) {
  controller_.SetPointerInHotZone(true);
  Advance(kDelay);
  ASSERT_EQ(controller_.state(), S::kShowing);
  controller_.SetPointerInHotZone(false);
  Advance(kShow - base::Milliseconds(10));
  EXPECT_EQ(controller_.state(), S::kShowing);
  Advance(base::Milliseconds(10));
  // Settled: now the leave counts, and the keep-open time starts at the settle.
  EXPECT_EQ(controller_.state(), S::kLeaving);
  Advance(kLeave - kEdge);
  EXPECT_EQ(controller_.state(), S::kLeaving);
  Advance(kEdge);
  EXPECT_EQ(controller_.state(), S::kHiding);
}

TEST_F(PeekTest, ZenALeaveAndReturnDuringTheSlideInKeepsItOpen) {
  controller_.SetPointerInHotZone(true);
  Advance(kDelay);
  controller_.SetPointerInHotZone(false);
  Advance(base::Milliseconds(100));
  controller_.SetPointerOverSidebar(true);
  Advance(kShow);
  EXPECT_EQ(controller_.state(), S::kShown);
}

// A hide that is reversed: the machine goes back to showing and asks to show
// again, so the view can start from the current offset.
TEST_F(PeekTest, ZenAReversingHideAsksToShowAgainFromWhereItIs) {
  OpenByHover();
  controller_.SetPointerOverSidebar(false);
  Advance(kLeave + base::Milliseconds(60));
  ASSERT_EQ(controller_.state(), S::kHiding);
  recorder_.log.clear();
  controller_.SetPointerOverSidebar(true);
  EXPECT_EQ(controller_.state(), S::kShowing);
  EXPECT_EQ(recorder_.log,
            (std::vector<std::string>{"show", "hiding>showing"}));
  // The hide's own timer is gone: it does not finish the hide.
  Advance(kHide);
  EXPECT_EQ(controller_.state(), S::kShowing);
  Advance(kShow);
  EXPECT_EQ(controller_.state(), S::kShown);
  ExpectTimerMatchesState();
}

// ---- One timer, no idle work ----

TEST_F(PeekTest, AtMostOneTimerIsPendingThroughAFullCycle) {
  ExpectTimerMatchesState();
  controller_.SetPointerInHotZone(true);
  ExpectTimerMatchesState();
  Advance(kDelay);
  ExpectTimerMatchesState();
  Advance(kShow);
  ExpectTimerMatchesState();
  controller_.SetPointerInHotZone(false);
  ExpectTimerMatchesState();
  Advance(kLeave);
  ExpectTimerMatchesState();
  controller_.SetPointerInHotZone(true);
  ExpectTimerMatchesState();
  Advance(kShow);
  ExpectTimerMatchesState();
  controller_.SetPointerInHotZone(false);
  controller_.ToggleKeepOpen();
  ExpectTimerMatchesState();
  controller_.ToggleKeepOpen();
  ExpectTimerMatchesState();
  Advance(kLeave + kHide);
  ExpectTimerMatchesState();
  EXPECT_EQ(controller_.state(), S::kHidden);
}

TEST_F(PeekTest, AnIdleControllerHasNoTimerAndNoPendingTask) {
  EXPECT_FALSE(controller_.timer_pending());
  EXPECT_EQ(task_environment_.GetPendingMainThreadTaskCount(), 0u);
  Advance(base::Hours(1));
  EXPECT_EQ(controller_.state(), S::kHidden);
}

TEST_F(PeekTest, ASettledShownSidebarHasNoTimer) {
  OpenByHover();
  EXPECT_FALSE(controller_.timer_pending());
  EXPECT_EQ(task_environment_.GetPendingMainThreadTaskCount(), 0u);
}

}  // namespace
}  // namespace aerie_sidebar
