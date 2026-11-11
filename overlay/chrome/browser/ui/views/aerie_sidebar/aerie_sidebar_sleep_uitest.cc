// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

// Interactive UI tests of sleeping tabs in the sidebar (issue #179): the favicon
// fade animates opacity only, on `fast` and `standard`, and reduced motion (the
// operating system's flag, and the in-app setting) swaps the look at once with
// no animation; the Menu key opens the context menu of a row and "Sleep tab" is
// reachable with the keyboard. Run with
// ./aerie test-cpp --target aerie_interactive_ui_tests.

#include <optional>
#include <string>

#include "base/test/run_until.h"
#include "base/time/time.h"
#include "chrome/browser/aerie_tab_model/sleep_controller.h"
#include "chrome/browser/aerie_tab_model/sleep_controller_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_context_menu.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_row_view.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_view.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/interactive_test_utils.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/aerie_design_tokens/aerie_motion_prefs.h"
#include "components/aerie_design_tokens/aerie_tokens.h"
#include "components/prefs/pref_service.h"
#include "content/public/test/browser_test.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/layer_animation_element.h"
#include "ui/compositor/layer_animation_sequence.h"
#include "ui/compositor/layer_animator.h"
#include "ui/compositor/test/layer_animator_test_controller.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/animation/animation_test_api.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/menu/menu_item_view.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"

namespace {

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

// True when no property animates on `layer`.
bool NothingAnimates(ui::Layer* layer) {
  return !layer->GetAnimator()->is_animating();
}

// True when nothing but opacity animates on `layer`.
bool OnlyOpacityAnimates(ui::Layer* layer) {
  for (int bit = ui::LayerAnimationElement::FIRST_PROPERTY;
       bit != ui::LayerAnimationElement::SENTINEL; bit <<= 1) {
    const auto property = static_cast<ui::LayerAnimationElement::AnimatableProperty>(bit);
    const bool animates = layer->GetAnimator()->IsAnimatingProperty(property);
    if (property == ui::LayerAnimationElement::OPACITY) {
      if (!animates) {
        ADD_FAILURE() << "opacity does not animate";
        return false;
      }
    } else if (animates) {
      ADD_FAILURE() << "property " << bit << " animates";
      return false;
    }
  }
  return true;
}

}  // namespace

class AerieSidebarSleepUiTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));
    ASSERT_TRUE(base::test::RunUntil(
        [&] { return sidebar() && sidebar()->row_count() == 1u; }));
    host_resolver()->AddRule("*", "127.0.0.1");
    ASSERT_TRUE(embedded_test_server()->Start());
    controller_ = aerie_tab_model::SleepControllerFactory::GetForProfile(
        browser()->GetProfile());
    ASSERT_TRUE(controller_);
  }

  void TearDownOnMainThread() override {
    controller_ = nullptr;
    reduced_motion_.reset();
    InProcessBrowserTest::TearDownOnMainThread();
  }

  void ForceReducedMotion(bool reduced) {
    reduced_motion_.reset();
    reduced_motion_.emplace(
        gfx::AnimationTestApi::SetPrefersReducedMotionForTesting(reduced));
  }

  AerieSidebarView* sidebar() {
    return FindDescendant<AerieSidebarView>(
        BrowserView::GetBrowserViewForBrowser(browser())
            ->vertical_tab_strip_region_view_for_testing());
  }
  PrefService* prefs() { return browser()->GetProfile()->GetPrefs(); }
  TabStripModel* strip() { return browser()->tab_strip_model(); }
  views::View* focused() {
    return BrowserView::GetBrowserViewForBrowser(browser())
        ->GetWidget()
        ->GetFocusManager()
        ->GetFocusedView();
  }

  AerieSidebarRowView* OpenBackgroundTab() {
    const size_t before = sidebar()->row_count();
    EXPECT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
        browser(), embedded_test_server()->GetURL("/title1.html"),
        WindowOpenDisposition::NEW_BACKGROUND_TAB,
        ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
    EXPECT_TRUE(base::test::RunUntil(
        [&] { return sidebar()->row_count() == before + 1; }));
    return sidebar()->row_at(before);
  }

  // Focus the list (it goes to the row with the stop, the selected tab) and
  // walk to `row` with the arrow keys.
  void FocusRow(AerieSidebarRowView* row) {
    sidebar()->today_tree()->RequestFocus();
    for (size_t i = 0; i < sidebar()->row_count() && focused() != row; ++i) {
      Press(ui::VKEY_DOWN);
    }
    ASSERT_EQ(focused(), row);
  }

  void Press(ui::KeyboardCode key) {
    ASSERT_TRUE(ui_test_utils::SendKeyPressSync(browser(), key, false, false,
                                                false, false));
  }

  raw_ptr<aerie_tab_model::SleepController> controller_ = nullptr;

 private:
  std::optional<gfx::AnimationTestApi::PrefersReducedMotionResetter>
      reduced_motion_;
};

IN_PROC_BROWSER_TEST_F(AerieSidebarSleepUiTest,
                       TheFaviconFadesOnFastAndStandardAnimatingOnlyOpacityThenDesaturatesStatically) {
  ForceReducedMotion(false);
  AerieSidebarRowView* row = OpenBackgroundTab();
  ASSERT_TRUE(controller_->Sleep(strip()->GetTabAtIndex(1)));
  ASSERT_TRUE(base::test::RunUntil([&] { return row->sleeping(); }));

  ui::Layer* layer = row->favicon_view()->layer();
  ASSERT_TRUE(layer);
  EXPECT_TRUE(OnlyOpacityAnimates(layer));
  EXPECT_FLOAT_EQ(layer->GetTargetOpacity(), 0.55f);
  // `fast`: one element with the token's duration.
  ui::LayerAnimatorTestController test_controller(layer->GetAnimator());
  ui::LayerAnimationSequence* sequence =
      test_controller.GetRunningSequence(ui::LayerAnimationElement::OPACITY);
  ASSERT_TRUE(sequence);
  EXPECT_EQ(sequence->FirstElement()->duration(),
            base::Milliseconds(aerie::tokens::GetDurationMs(
                aerie::tokens::MotionDuration::kFast, false)));
  // The desaturate is not an animation, and it comes after the fade.
  EXPECT_FLOAT_EQ(layer->GetTargetGrayscale(), 0.f);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return layer->GetTargetGrayscale() == 1.f; }));
  EXPECT_FALSE(layer->GetAnimator()->IsAnimatingProperty(
      ui::LayerAnimationElement::GRAYSCALE));
}

enum class ReduceVia { kOsFlag, kPref };

class AerieSidebarSleepReducedMotionUiTest
    : public AerieSidebarSleepUiTest,
      public testing::WithParamInterface<ReduceVia> {};

INSTANTIATE_TEST_SUITE_P(
    AerieReduceVia,
    AerieSidebarSleepReducedMotionUiTest,
    testing::Values(ReduceVia::kOsFlag, ReduceVia::kPref),
    [](const testing::TestParamInfo<ReduceVia>& info) {
      return info.param == ReduceVia::kOsFlag ? "OsFlag" : "Pref";
    });

IN_PROC_BROWSER_TEST_P(AerieSidebarSleepReducedMotionUiTest,
                       ReducedMotionSwapsTheLookAtOnceAndCreatesNoAnimation) {
  if (GetParam() == ReduceVia::kOsFlag) {
    ForceReducedMotion(true);
  } else {
    ForceReducedMotion(false);
    prefs()->SetString(aerie::motion::kReduceMotionPref,
                       aerie::motion::kReduceMotionOn);
  }
  AerieSidebarRowView* row = OpenBackgroundTab();
  ASSERT_TRUE(controller_->Sleep(strip()->GetTabAtIndex(1)));
  ASSERT_TRUE(base::test::RunUntil([&] { return row->sleeping(); }));

  ui::Layer* layer = row->favicon_view()->layer();
  ASSERT_TRUE(layer);
  // The first frame after the change already has both looks.
  EXPECT_TRUE(NothingAnimates(layer));
  EXPECT_FLOAT_EQ(layer->opacity(), 0.55f);
  EXPECT_FLOAT_EQ(layer->GetTargetGrayscale(), 1.f);
  EXPECT_TRUE(row->sleep_badge()->GetVisible());
}

IN_PROC_BROWSER_TEST_F(AerieSidebarSleepUiTest,
                       TheMenuKeyOpensTheContextMenuAndSleepTabIsReachable) {
  ForceReducedMotion(true);
  AerieSidebarRowView* row = OpenBackgroundTab();
  FocusRow(row);

  Press(ui::VKEY_APPS);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return sidebar()->context_menu() && sidebar()->context_menu()->is_running(); }));
  views::MenuItemView* item = sidebar()->context_menu()->sleep_item();
  ASSERT_TRUE(item);
  EXPECT_TRUE(sidebar()->context_menu()->IsCommandEnabled(
      AerieSidebarContextMenu::kCommandSleepTab));
  // The menu also has the Pin and Favorites items above it (issue #180): walk
  // down to "Sleep tab".
  for (int i = 0; i < 6 && !item->IsSelected(); ++i) {
    Press(ui::VKEY_DOWN);
  }
  ASSERT_TRUE(item->IsSelected());
  Press(ui::VKEY_RETURN);
  EXPECT_TRUE(base::test::RunUntil([&] { return row->sleeping(); }));
  EXPECT_EQ(strip()->active_index(), 0);
}

IN_PROC_BROWSER_TEST_F(AerieSidebarSleepUiTest,
                       EscClosesTheContextMenuWithoutSleepingTheTab) {
  ForceReducedMotion(true);
  AerieSidebarRowView* row = OpenBackgroundTab();
  FocusRow(row);
  Press(ui::VKEY_APPS);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return sidebar()->context_menu() && sidebar()->context_menu()->is_running(); }));
  Press(ui::VKEY_ESCAPE);
  EXPECT_TRUE(base::test::RunUntil(
      [&] { return !sidebar()->context_menu()->is_running(); }));
  EXPECT_FALSE(row->sleeping());
}
