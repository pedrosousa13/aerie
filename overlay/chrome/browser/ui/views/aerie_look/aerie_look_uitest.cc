// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

// Reduced motion and the theme change (issue #185): with the operating
// system's flag or the app's own preference on, a change in Settings applies at
// once and no animation is created.

#include <optional>
#include <string>

#include "base/run_loop.h"
#include "base/task/single_thread_task_runner.h"
#include "base/time/time.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/views/aerie_frame/frame_tint_view.h"
#include "chrome/browser/ui/views/aerie_look/sidebar_look_controller.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/interactive_test_utils.h"
#include "components/aerie_design_tokens/aerie_appearance_prefs.h"
#include "components/aerie_design_tokens/aerie_motion_prefs.h"
#include "components/aerie_design_tokens/aerie_tokens.h"
#include "components/prefs/pref_service.h"
#include "content/public/test/browser_test.h"
#include "ui/aura/window.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/layer_animator.h"
#include "ui/gfx/animation/animation_test_api.h"
#include "ui/views/widget/widget.h"

namespace {

enum class Source { kOperatingSystem, kAppPreference };

class AerieLookReducedMotionUiTest
    : public InProcessBrowserTest,
      public testing::WithParamInterface<Source> {
 protected:
  BrowserView* browser_view() {
    return BrowserView::GetBrowserViewForBrowser(browser());
  }
  FrameTintView* tint() { return browser_view()->frame_tint_view_for_testing(); }
  PrefService* prefs() { return browser()->GetProfile()->GetPrefs(); }

  void Pump(base::TimeDelta d) {
    base::RunLoop loop;
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, loop.QuitClosure(), d);
    loop.Run();
  }

  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));
    if (GetParam() == Source::kOperatingSystem) {
      os_flag_.emplace(
          gfx::AnimationTestApi::SetPrefersReducedMotionForTesting(true));
      prefs()->SetString(aerie::motion::kReduceMotionPref,
                         aerie::motion::kReduceMotionSystem);
    } else {
      os_flag_.emplace(
          gfx::AnimationTestApi::SetPrefersReducedMotionForTesting(false));
      prefs()->SetString(aerie::motion::kReduceMotionPref,
                         aerie::motion::kReduceMotionOn);
    }
  }

  static int CountLayers(ui::Layer* layer, int* animating) {
    int n = 1;
    if (layer->GetAnimator() && layer->GetAnimator()->is_animating()) {
      ++*animating;
    }
    for (ui::Layer* child : layer->children()) {
      n += CountLayers(child, animating);
    }
    return n;
  }

  std::optional<gfx::AnimationTestApi::PrefersReducedMotionResetter> os_flag_;
};

IN_PROC_BROWSER_TEST_P(AerieLookReducedMotionUiTest, AThemeChangeCreatesNoAnimation) {
  ui::Layer* root = browser_view()->GetWidget()->GetNativeView()->layer();
  int animating_before = 0;
  const int layers_before = CountLayers(root, &animating_before);
  LookCrossfade& fade =
      tint()->look_controller()->crossfade_for_testing();

  for (const char* id : {"soft", "clean", "high-contrast", "mono", "vivid"}) {
    prefs()->SetString(aerie::appearance::kThemePref, id);
    // Straight after the change, before any frame: nothing animates.
    EXPECT_FALSE(fade.running()) << id;
    EXPECT_EQ(fade.snapshot_layer_for_testing(), nullptr) << id;
    int animating = 0;
    const int layers = CountLayers(root, &animating);
    EXPECT_EQ(layers, layers_before) << id << " added a layer";
    EXPECT_EQ(animating, animating_before) << id << " started an animation";
  }
  for (const char* mode : {"dark", "light", "system"}) {
    prefs()->SetString(aerie::appearance::kModePref, mode);
    EXPECT_FALSE(fade.running()) << mode;
    int animating = 0;
    EXPECT_EQ(CountLayers(root, &animating), layers_before) << mode;
    EXPECT_EQ(animating, animating_before) << mode;
  }
  Pump(base::Milliseconds(500));
  EXPECT_EQ(fade.finished_count(), 0);
  // The look still applied.
  EXPECT_EQ(tint()->inputs_for_testing().theme, aerie::tokens::ThemeId::kVivid);
}

INSTANTIATE_TEST_SUITE_P(
    All, AerieLookReducedMotionUiTest,
    testing::Values(Source::kOperatingSystem, Source::kAppPreference),
    [](const testing::TestParamInfo<Source>& info) {
      return info.param == Source::kOperatingSystem ? "OperatingSystem"
                                                    : "AppPreference";
    });

}  // namespace
