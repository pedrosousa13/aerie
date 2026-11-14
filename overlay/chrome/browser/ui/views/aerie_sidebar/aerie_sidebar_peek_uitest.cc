// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

// The auto-hide peek of the sidebar (issue #184), with real windows and real
// keys: the slide moves only transform and opacity, reduced motion fades for
// 100 ms with no transform and leaves the delays alone, F6 opens a hidden
// sidebar and keeps it open while focus is inside, and Ctrl+Alt+S keeps it open
// and says so. Run with ./aerie test-cpp --target aerie_interactive_ui_tests.

#include <optional>

#include "base/test/run_until.h"
#include "base/time/time.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_peek_driver.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_view.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/multi_contents_view.h"
#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/interactive_test_utils.h"
#include "components/aerie_design_tokens/aerie_motion_prefs.h"
#include "components/aerie_design_tokens/aerie_tokens.h"
#include "components/aerie_sidebar/aerie_sidebar_peek.h"
#include "components/aerie_sidebar/aerie_sidebar_prefs.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "ui/aura/window.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/layer_animation_element.h"
#include "ui/compositor/layer_animation_sequence.h"
#include "ui/compositor/layer_animator.h"
#include "ui/compositor/test/layer_animator_test_controller.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/events/test/event_generator.h"
#include "ui/gfx/animation/animation_test_api.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"

namespace {

using aerie_sidebar::PeekState;
using Property = ui::LayerAnimationElement::AnimatableProperty;

template <typename T>
T* FindDescendant(views::View* root) {
  if (auto* match = views::AsViewClass<T>(root)) {
    return match;
  }
  for (views::View* child : root->children()) {
    if (T* found = FindDescendant<T>(child)) {
      return found;
    }
  }
  return nullptr;
}

bool OnlyTransformAndOpacityAnimate(ui::Layer* layer) {
  for (int bit = ui::LayerAnimationElement::FIRST_PROPERTY;
       bit != ui::LayerAnimationElement::SENTINEL; bit <<= 1) {
    const auto property = static_cast<Property>(bit);
    if (property == ui::LayerAnimationElement::TRANSFORM ||
        property == ui::LayerAnimationElement::OPACITY) {
      continue;
    }
    if (layer->GetAnimator()->IsAnimatingProperty(property)) {
      ADD_FAILURE() << "property " << bit << " animates";
      return false;
    }
  }
  return true;
}

base::TimeDelta OpacityDuration(ui::Layer* layer) {
  ui::LayerAnimatorTestController controller(layer->GetAnimator());
  ui::LayerAnimationSequence* sequence =
      controller.GetRunningSequence(ui::LayerAnimationElement::OPACITY);
  return sequence ? sequence->FirstElement()->duration() : base::TimeDelta();
}

}  // namespace

class AerieSidebarPeekUiTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));
    browser_view()->GetWidget()->SetBounds(gfx::Rect(0, 0, 1100, 750));
    ASSERT_TRUE(base::test::RunUntil(
        [&] { return sidebar() && sidebar()->row_count() == 1u; }));
    // What the settings setter does for auto-hide: undock and choose it.
    prefs()->SetString(aerie_sidebar::kDensityPref,
                       aerie_sidebar::kDensityAutoHide);
    prefs()->SetBoolean(aerie_sidebar::kVisiblePref, false);
    ASSERT_TRUE(base::test::RunUntil([&] { return peek()->active(); }));
    // The page takes the room the sidebar leaves.
    ASSERT_TRUE(base::test::RunUntil([&] {
      return browser_view()->multi_contents_view()->x() == 0;
    }));
    FocusPage();
  }

  void ForceReducedMotion(bool reduced) {
    reduced_motion_.reset();
    reduced_motion_.emplace(
        gfx::AnimationTestApi::SetPrefersReducedMotionForTesting(reduced));
  }

  BrowserView* browser_view() {
    return BrowserView::GetBrowserViewForBrowser(browser());
  }
  VerticalTabStripRegionView* region() {
    return browser_view()->vertical_tab_strip_region_view_for_testing();
  }
  AerieSidebarView* sidebar() {
    return FindDescendant<AerieSidebarView>(region());
  }
  AerieSidebarPeekDriver* peek() { return sidebar()->peek_driver(); }
  PrefService* prefs() { return browser()->GetProfile()->GetPrefs(); }

  void FocusPage() {
    browser_view()->GetFocusManager()->ClearFocus();
    browser()->tab_strip_model()->GetActiveWebContents()->Focus();
  }
  void MovePointerTo(const gfx::Point& screen_point) {
    views::Widget* widget = browser_view()->GetWidget();
    ui::test::EventGenerator generator(
        widget->GetNativeWindow()->GetRootWindow(), widget->GetNativeWindow());
    generator.MoveMouseRelativeTo(
        widget->GetNativeWindow(),
        screen_point - widget->GetWindowBoundsInScreen().OffsetFromOrigin());
  }
  void PointerToTheEdge() {
    MovePointerTo(peek()->HotZoneInScreen().CenterPoint());
  }
  void PointerToThePage() {
    MovePointerTo(browser_view()->GetBoundsInScreen().CenterPoint());
  }
  bool Reaches(PeekState state) {
    return base::test::RunUntil(
        [&] { return peek()->controller().state() == state; });
  }
  void PressKeyAccelerator(ui::KeyboardCode key, int flags) {
    browser_view()->GetFocusManager()->ProcessAccelerator(
        ui::Accelerator(key, flags));
  }

 private:
  std::optional<gfx::AnimationTestApi::PrefersReducedMotionResetter>
      reduced_motion_;
};

// The slide moves the column's layer with a transform and an opacity, and no
// other property, and the page card does not move.
IN_PROC_BROWSER_TEST_F(AerieSidebarPeekUiTest,
                       ThePeekSlidesOnlyTransformAndOpacityAndNotThePage) {
  ForceReducedMotion(false);
  const gfx::Rect page = browser_view()->multi_contents_view()->bounds();
  PointerToTheEdge();
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return peek()->controller().state() == PeekState::kShowing; }));
  ui::Layer* layer = region()->layer();
  ASSERT_TRUE(layer);
  EXPECT_TRUE(layer->GetAnimator()->IsAnimatingProperty(
      ui::LayerAnimationElement::TRANSFORM));
  EXPECT_TRUE(OnlyTransformAndOpacityAnimate(layer));
  ASSERT_TRUE(Reaches(PeekState::kShown) || Reaches(PeekState::kLeaving));
  EXPECT_TRUE(layer->GetTargetTransform().IsIdentity());
  EXPECT_EQ(browser_view()->multi_contents_view()->bounds(), page);
}

// Reduced motion: a 100 ms fade, no transform, and the delays are the same
// (issue #184). Run with the operating system's flag and with the setting.
enum class ReduceVia { kOsFlag, kPref };

class AerieSidebarPeekReducedMotionUiTest
    : public AerieSidebarPeekUiTest,
      public testing::WithParamInterface<ReduceVia> {};

INSTANTIATE_TEST_SUITE_P(
    AerieReduceVia,
    AerieSidebarPeekReducedMotionUiTest,
    testing::Values(ReduceVia::kOsFlag, ReduceVia::kPref),
    [](const testing::TestParamInfo<ReduceVia>& info) {
      return info.param == ReduceVia::kOsFlag ? "OsFlag" : "Pref";
    });

IN_PROC_BROWSER_TEST_P(AerieSidebarPeekReducedMotionUiTest,
                       TheFadeIsOneHundredMillisecondsWithNoTransform) {
  if (GetParam() == ReduceVia::kOsFlag) {
    ForceReducedMotion(true);
  } else {
    ForceReducedMotion(false);
    prefs()->SetString(aerie::motion::kReduceMotionPref,
                       aerie::motion::kReduceMotionOn);
  }
  const base::TimeTicks start = base::TimeTicks::Now();
  PointerToTheEdge();
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return peek()->controller().state() == PeekState::kShowing; }));
  // The delay did not change: the sidebar came after the full 150 ms.
  EXPECT_GE(base::TimeTicks::Now() - start,
            base::Milliseconds(aerie::tokens::kMotionTimingPeekDelay));
  ui::Layer* layer = region()->layer();
  ASSERT_TRUE(layer);
  EXPECT_FALSE(layer->GetAnimator()->IsAnimatingProperty(
      ui::LayerAnimationElement::TRANSFORM));
  EXPECT_TRUE(layer->GetTargetTransform().IsIdentity());
  EXPECT_TRUE(OnlyTransformAndOpacityAnimate(layer));
  EXPECT_EQ(OpacityDuration(layer), base::Milliseconds(100));
  // The controller's timings are the unreduced delays and grace; only the
  // slide's duration is the fade's.
  const aerie_sidebar::PeekTimings reduced =
      aerie_sidebar::PeekTimingsFor(true);
  const aerie_sidebar::PeekTimings normal =
      aerie_sidebar::PeekTimingsFor(false);
  EXPECT_EQ(reduced.delay, normal.delay);
  EXPECT_EQ(reduced.leave, normal.leave);
  EXPECT_EQ(reduced.grace_x, normal.grace_x);
  EXPECT_EQ(reduced.grace_y, normal.grace_y);
  EXPECT_EQ(reduced.show, base::Milliseconds(100));
  EXPECT_EQ(reduced.hide, base::Milliseconds(100));
  // The sidebar in force of the reduced timings now, and the hide fades for
  // 100 ms as well.
  EXPECT_EQ(peek()->controller().timings().hide, base::Milliseconds(100));
  ASSERT_TRUE(Reaches(PeekState::kShown) || Reaches(PeekState::kLeaving));
  PointerToThePage();
  ASSERT_TRUE(Reaches(PeekState::kHiding));
  EXPECT_EQ(OpacityDuration(layer), base::Milliseconds(100));
}

// A change of the reduced-motion setting is in force for the very next peek:
// the controller is given the timings before it takes the pointer, not after
// the slide begins (issue #184 review).
IN_PROC_BROWSER_TEST_P(AerieSidebarPeekReducedMotionUiTest,
                       TheFirstPeekAfterTheChangeUsesTheReducedTimings) {
  ForceReducedMotion(false);
  // Peek once with motion on, so that the controller holds the full timings.
  PointerToTheEdge();
  ASSERT_TRUE(Reaches(PeekState::kShown));
  PointerToThePage();
  ASSERT_TRUE(Reaches(PeekState::kHidden));
  ASSERT_EQ(peek()->controller().timings().show,
            aerie_sidebar::PeekTimingsFor(false).show);
  if (GetParam() == ReduceVia::kOsFlag) {
    ForceReducedMotion(true);
  } else {
    prefs()->SetString(aerie::motion::kReduceMotionPref,
                       aerie::motion::kReduceMotionOn);
  }
  // The pointer reaches the edge: the timings are the reduced ones before the
  // arming timer starts, not only once the sidebar slides.
  PointerToTheEdge();
  EXPECT_EQ(peek()->controller().state(), PeekState::kArming);
  EXPECT_EQ(peek()->controller().timings().show,
            aerie_sidebar::PeekTimingsFor(true).show);
  EXPECT_EQ(peek()->controller().timings().hide,
            aerie_sidebar::PeekTimingsFor(true).hide);
}

// F6 opens a hidden sidebar, moves focus into it, and holds it open while the
// focus is inside, even with the pointer away.
IN_PROC_BROWSER_TEST_F(AerieSidebarPeekUiTest, F6OpensTheSidebarAndHoldsItOpen) {
  ASSERT_EQ(peek()->controller().state(), PeekState::kHidden);
  ASSERT_TRUE(ui_test_utils::SendKeyPressSync(browser(), ui::VKEY_F6, false,
                                              false, false, false));
  ASSERT_TRUE(base::test::RunUntil([&] {
    views::View* focused = browser_view()->GetFocusManager()->GetFocusedView();
    return focused && sidebar()->Contains(focused);
  }));
  EXPECT_TRUE(peek()->controller().visible());
  EXPECT_TRUE(sidebar()->GetVisible());
  // The pointer is on the page, and the sidebar stays: focus is inside.
  PointerToThePage();
  base::RunLoop loop;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, loop.QuitClosure(), base::Milliseconds(700));
  loop.Run();
  EXPECT_TRUE(peek()->controller().visible());
  EXPECT_TRUE(sidebar()->GetVisible());
  // Focus goes back to the page: the sidebar may go.
  FocusPage();
  ASSERT_TRUE(Reaches(PeekState::kHidden));
}

// Ctrl+Alt+S keeps the sidebar open and shows a toast; again, and it hides when
// the pointer is away. It does nothing when the sidebar is not auto-hidden.
IN_PROC_BROWSER_TEST_F(AerieSidebarPeekUiTest, CtrlAltSKeepsTheSidebarOpen) {
  PointerToTheEdge();
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return peek()->controller().visible(); }));
  PressKeyAccelerator(ui::VKEY_S, ui::EF_CONTROL_DOWN | ui::EF_ALT_DOWN);
  EXPECT_TRUE(peek()->controller().keep_open());
  EXPECT_TRUE(sidebar()->toast_visible());
  const std::u16string kept(sidebar()->toast()->GetText());
  // The pointer leaves; it stays.
  PointerToThePage();
  base::RunLoop loop;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, loop.QuitClosure(), base::Milliseconds(700));
  loop.Run();
  EXPECT_TRUE(peek()->controller().visible());
  PressKeyAccelerator(ui::VKEY_S, ui::EF_CONTROL_DOWN | ui::EF_ALT_DOWN);
  EXPECT_FALSE(peek()->controller().keep_open());
  EXPECT_NE(std::u16string(sidebar()->toast()->GetText()), kept);
  ASSERT_TRUE(Reaches(PeekState::kHidden));

  // Docked (not auto-hidden): the key is not handled.
  prefs()->SetString(aerie_sidebar::kDensityPref,
                     aerie_sidebar::kDensityComfortable);
  ASSERT_TRUE(base::test::RunUntil([&] { return !peek()->active(); }));
  EXPECT_FALSE(browser_view()->GetFocusManager()->ProcessAccelerator(
      ui::Accelerator(ui::VKEY_S, ui::EF_CONTROL_DOWN | ui::EF_ALT_DOWN)));
}
