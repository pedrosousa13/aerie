// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

// The motion and the keyboard of the horizontal tab layout (issue #186;
// docs/sidebar-design.md, "Horizontal tabs" and "Motion"). The tests read the
// layer animators of the strip's tabs: only transform and opacity animate (the
// strip tab's width is Chromium's layout animation), the open and close take the
// token durations, and under reduced motion (the OS flag, and the in-app setting)
// the open fades in 140 ms or less, the close in 90 ms or less, with no
// transform. Ctrl+Tab, Ctrl+Shift+Tab and Ctrl+1..9 move the selection.
// Run with ./aerie test-cpp --target aerie_interactive_ui_tests.

#include <optional>
#include <string>
#include <vector>

#include "base/run_loop.h"
#include "base/test/run_until.h"
#include "base/time/time.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/tab_strip_region_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/interactive_test_utils.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/aerie_design_tokens/aerie_motion_prefs.h"
#include "components/aerie_sidebar/aerie_sidebar_prefs.h"
#include "components/prefs/pref_service.h"
#include "content/public/test/browser_test.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/layer_animation_element.h"
#include "ui/compositor/layer_animation_sequence.h"
#include "ui/compositor/layer_animator.h"
#include "ui/compositor/test/layer_animator_test_controller.h"
#include "ui/gfx/animation/animation_test_api.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/test/views_test_utils.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace {

using Property = ui::LayerAnimationElement::AnimatableProperty;

void CollectTabs(views::View* root, std::vector<views::View*>* out) {
  ui::AXNodeData data;
  root->GetViewAccessibility().GetAccessibleNodeData(&data);
  if (data.role == ax::mojom::Role::kTab) {
    out->push_back(root);
  }
  for (views::View* child : root->children()) {
    CollectTabs(child, out);
  }
}

bool Animates(ui::Layer* layer, Property property) {
  return layer->GetAnimator()->IsAnimatingProperty(property);
}

bool OnlyTransformAndOpacityAnimate(ui::Layer* layer) {
  for (int bit = ui::LayerAnimationElement::FIRST_PROPERTY;
       bit != ui::LayerAnimationElement::SENTINEL; bit <<= 1) {
    const auto property = static_cast<Property>(bit);
    if (property == ui::LayerAnimationElement::TRANSFORM ||
        property == ui::LayerAnimationElement::OPACITY) {
      continue;
    }
    if (Animates(layer, property)) {
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

class AerieHzLayoutUiTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));
    browser_view()->GetWidget()->SetBounds(gfx::Rect(0, 0, 1100, 750));
    ForceReducedMotion(false);
    prefs()->SetString(aerie_sidebar::kLayoutPref,
                       aerie_sidebar::kLayoutHorizontal);
    ASSERT_TRUE(base::test::RunUntil([&] { return !Tabs().empty(); }));
  }

  void TearDownOnMainThread() override {
    reduced_motion_.reset();
    InProcessBrowserTest::TearDownOnMainThread();
  }

  void ForceReducedMotion(bool reduced) {
    reduced_motion_.reset();
    reduced_motion_.emplace(
        gfx::AnimationTestApi::SetPrefersReducedMotionForTesting(reduced));
  }
  BrowserView* browser_view() {
    return BrowserView::GetBrowserViewForBrowser(browser());
  }
  PrefService* prefs() { return browser()->GetProfile()->GetPrefs(); }
  TabStripModel* strip() { return browser()->tab_strip_model(); }
  std::vector<views::View*> Tabs() {
    std::vector<views::View*> tabs;
    CollectTabs(browser_view()->tab_strip_view(), &tabs);
    return tabs;
  }

  // Opens a tab and returns the strip's view of it, while it animates.
  views::View* OpenTab() {
    const size_t before = Tabs().size();
    chrome::ExecuteCommand(browser(), IDC_NEW_TAB);
    EXPECT_TRUE(base::test::RunUntil([&] { return Tabs().size() == before + 1; }));
    return Tabs().back();
  }

  // Closes the last tab and waits until the strip has taken `tab` over for its
  // close animation (it does that in a task after the model change; the view
  // stays in the strip until the animation ends).
  void CloseLastTab(views::View* tab) {
    strip()->CloseWebContentsAt(strip()->count() - 1,
                                TabCloseTypes::CLOSE_USER_GESTURE);
    EXPECT_TRUE(base::test::RunUntil([&] { return tab->layer() != nullptr; }));
  }

  void Key(ui::KeyboardCode key, bool control, bool shift) {
    ASSERT_TRUE(ui_test_utils::SendKeyPressSync(browser(), key, control, shift,
                                                false, false));
  }

 private:
  std::optional<gfx::AnimationTestApi::PrefersReducedMotionResetter>
      reduced_motion_;
};

IN_PROC_BROWSER_TEST_F(AerieHzLayoutUiTest,
                       OpeningATabAnimatesOnlyTransformAndOpacityOverTabOpen) {
  views::View* tab = OpenTab();
  ASSERT_TRUE(tab->layer());
  ui::Layer* layer = tab->layer();
  EXPECT_TRUE(Animates(layer, ui::LayerAnimationElement::TRANSFORM));
  EXPECT_TRUE(Animates(layer, ui::LayerAnimationElement::OPACITY));
  EXPECT_TRUE(OnlyTransformAndOpacityAnimate(layer));
  // motion-duration-tab-open, 200 ms.
  EXPECT_EQ(OpacityDuration(layer), base::Milliseconds(200));
  EXPECT_TRUE(layer->GetTargetTransform().IsIdentity());
  EXPECT_EQ(layer->GetTargetOpacity(), 1.0f);
}

IN_PROC_BROWSER_TEST_F(AerieHzLayoutUiTest,
                       ClosingATabFadesAndSlidesOverXfastAndAnimatesOnlyThose) {
  OpenTab();
  views::View* tab = Tabs().back();
  // Let the open settle, so that the close is what animates.
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return !tab->layer() || !Animates(tab->layer(), ui::LayerAnimationElement::OPACITY); }));
  CloseLastTab(tab);
  ASSERT_TRUE(tab->layer());
  ui::Layer* layer = tab->layer();
  EXPECT_TRUE(Animates(layer, ui::LayerAnimationElement::OPACITY));
  EXPECT_TRUE(Animates(layer, ui::LayerAnimationElement::TRANSFORM));
  EXPECT_TRUE(OnlyTransformAndOpacityAnimate(layer));
  // motion-duration-xfast, 90 ms.
  EXPECT_EQ(OpacityDuration(layer), base::Milliseconds(90));
  EXPECT_EQ(layer->GetTargetOpacity(), 0.0f);
}

enum class ReduceVia { kOsFlag, kPref };

class AerieHzReducedMotionUiTest
    : public AerieHzLayoutUiTest,
      public testing::WithParamInterface<ReduceVia> {
 protected:
  void ReduceMotion() {
    if (GetParam() == ReduceVia::kOsFlag) {
      ForceReducedMotion(true);
      return;
    }
    ForceReducedMotion(false);
    prefs()->SetString(aerie::motion::kReduceMotionPref,
                       aerie::motion::kReduceMotionOn);
  }
};

INSTANTIATE_TEST_SUITE_P(
    AerieReduceVia,
    AerieHzReducedMotionUiTest,
    testing::Values(ReduceVia::kOsFlag, ReduceVia::kPref),
    [](const testing::TestParamInfo<ReduceVia>& info) {
      return info.param == ReduceVia::kOsFlag ? "OsFlag" : "Pref";
    });

IN_PROC_BROWSER_TEST_P(AerieHzReducedMotionUiTest,
                       OpenFadesAtMost140MsWithNoTransform) {
  ReduceMotion();
  views::View* tab = OpenTab();
  ASSERT_TRUE(tab->layer());
  ui::Layer* layer = tab->layer();
  EXPECT_FALSE(Animates(layer, ui::LayerAnimationElement::TRANSFORM));
  EXPECT_TRUE(OnlyTransformAndOpacityAnimate(layer));
  ASSERT_TRUE(Animates(layer, ui::LayerAnimationElement::OPACITY));
  EXPECT_LE(OpacityDuration(layer), base::Milliseconds(140));
}

IN_PROC_BROWSER_TEST_P(AerieHzReducedMotionUiTest,
                       CloseFadesAtMost90MsWithNoTransform) {
  OpenTab();
  views::View* tab = Tabs().back();
  ReduceMotion();
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return !tab->layer() || !Animates(tab->layer(), ui::LayerAnimationElement::OPACITY); }));
  CloseLastTab(tab);
  ASSERT_TRUE(tab->layer());
  EXPECT_FALSE(Animates(tab->layer(), ui::LayerAnimationElement::TRANSFORM));
  EXPECT_TRUE(OnlyTransformAndOpacityAnimate(tab->layer()));
  ASSERT_TRUE(Animates(tab->layer(), ui::LayerAnimationElement::OPACITY));
  EXPECT_LE(OpacityDuration(tab->layer()), base::Milliseconds(90));
}

IN_PROC_BROWSER_TEST_F(AerieHzLayoutUiTest, CtrlTabAndCtrlNumberMoveTheSelection) {
  OpenTab();
  OpenTab();
  OpenTab();
  ASSERT_EQ(strip()->count(), 4);
  strip()->ActivateTabAt(0);
  Key(ui::VKEY_TAB, /*control=*/true, /*shift=*/false);
  EXPECT_EQ(strip()->active_index(), 1);
  Key(ui::VKEY_TAB, true, false);
  EXPECT_EQ(strip()->active_index(), 2);
  Key(ui::VKEY_TAB, true, true);
  EXPECT_EQ(strip()->active_index(), 1);
  Key(ui::VKEY_3, true, false);
  EXPECT_EQ(strip()->active_index(), 2);
  Key(ui::VKEY_1, true, false);
  EXPECT_EQ(strip()->active_index(), 0);
  // Ctrl+9 is the last tab.
  Key(ui::VKEY_9, true, false);
  EXPECT_EQ(strip()->active_index(), 3);
}
