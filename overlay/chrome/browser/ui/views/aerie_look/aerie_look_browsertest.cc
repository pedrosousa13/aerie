// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

// Theme and mode for the whole browser (issue #185).

#include <string>

#include "base/run_loop.h"
#include "base/task/single_thread_task_runner.h"
#include "base/time/time.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/themes/theme_service_factory.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/views/aerie_frame/frame_tint_view.h"
#include "chrome/browser/ui/views/aerie_look/sidebar_look_controller.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "components/aerie_design_tokens/aerie_appearance_prefs.h"
#include "components/aerie_design_tokens/aerie_motion_prefs.h"
#include "components/aerie_design_tokens/aerie_tokens.h"
#include "components/prefs/pref_service.h"
#include "content/public/test/browser_test.h"
#include "ui/color/color_id.h"
#include "ui/native_theme/native_theme.h"
#include "ui/views/widget/widget.h"

namespace {

class AerieLookBrowserTest : public InProcessBrowserTest {
 protected:
  BrowserView* browser_view() {
    return BrowserView::GetBrowserViewForBrowser(browser());
  }
  FrameTintView* tint() { return browser_view()->frame_tint_view_for_testing(); }
  PrefService* prefs() { return browser()->GetProfile()->GetPrefs(); }
  LookCrossfade& fade() {
    return tint()->look_controller()->crossfade_for_testing();
  }
  void Pump(base::TimeDelta d) {
    base::RunLoop loop;
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, loop.QuitClosure(), d);
    loop.Run();
  }
  template <typename P>
  bool WaitFor(P done) {
    const base::TimeTicks end = base::TimeTicks::Now() + base::Seconds(10);
    while (!done() && base::TimeTicks::Now() < end) {
      Pump(base::Milliseconds(20));
    }
    return done();
  }
};

IN_PROC_BROWSER_TEST_F(AerieLookBrowserTest, EachThemeRedrawsTheFrame) {
  prefs()->SetString(aerie::motion::kReduceMotionPref,
                     aerie::motion::kReduceMotionOn);
  // OLED has no light table: it needs a dark mode to be the effective theme.
  prefs()->SetString(aerie::appearance::kModePref, "dark");
  for (const char* id : {"soft", "clean", "oled", "high-contrast", "mono"}) {
    prefs()->SetString(aerie::appearance::kThemePref, id);
    Pump(base::Milliseconds(50));
    EXPECT_EQ(aerie::tokens::kThemeIds[static_cast<int>(
                  tint()->inputs_for_testing().theme)],
              std::string(id));
  }
}

IN_PROC_BROWSER_TEST_F(AerieLookBrowserTest, ModeFollowsTheBrowserColourScheme) {
  prefs()->SetString(aerie::motion::kReduceMotionPref,
                     aerie::motion::kReduceMotionOn);
  prefs()->SetString(aerie::appearance::kModePref, "dark");
  Pump(base::Milliseconds(50));
  EXPECT_EQ(ThemeServiceFactory::GetForProfile(browser()->GetProfile())
                ->GetBrowserColorScheme(),
            ThemeService::BrowserColorScheme::kDark);
  EXPECT_EQ(tint()->inputs_for_testing().mode, aerie::tokens::ColorMode::kDark);
}

IN_PROC_BROWSER_TEST_F(AerieLookBrowserTest, ACrossfadeRunsAndLeavesNoLayer) {
  prefs()->SetString(aerie::motion::kReduceMotionPref,
                     aerie::motion::kReduceMotionSystem);
  prefs()->SetString(aerie::appearance::kThemePref, "soft");
  ASSERT_TRUE(fade().running());
  EXPECT_NEAR(fade().opacity(), 1.0f, 0.2f);
  ASSERT_TRUE(WaitFor([&] { return !fade().running(); }));
  EXPECT_EQ(fade().finished_count(), 1);
  EXPECT_EQ(fade().snapshot_layer_for_testing(), nullptr);
}

IN_PROC_BROWSER_TEST_F(AerieLookBrowserTest, ReducedMotionStartsNoFade) {
  prefs()->SetString(aerie::motion::kReduceMotionPref,
                     aerie::motion::kReduceMotionOn);
  prefs()->SetString(aerie::appearance::kThemePref, "clean");
  EXPECT_FALSE(fade().running());
}

IN_PROC_BROWSER_TEST_F(AerieLookBrowserTest, NoFadeAtLaunch) {
  EXPECT_FALSE(fade().running());
  EXPECT_EQ(fade().finished_count(), 0);
}

IN_PROC_BROWSER_TEST_F(AerieLookBrowserTest, ASecondChangeKeepsOneSnapshot) {
  prefs()->SetString(aerie::motion::kReduceMotionPref,
                     aerie::motion::kReduceMotionSystem);
  prefs()->SetString(aerie::appearance::kThemePref, "soft");
  Pump(base::Milliseconds(100));
  prefs()->SetString(aerie::appearance::kThemePref, "clean");
  EXPECT_TRUE(fade().running());
  ASSERT_TRUE(WaitFor([&] { return !fade().running(); }));
  EXPECT_EQ(fade().finished_count(), 1);
}

}  // namespace
