// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

// Theme and mode for the whole browser (issue #185).

#include <string>
#include <utility>
#include <vector>

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
#include "components/aerie_frame/aerie_frame_look.h"
#include "components/aerie_frame/aerie_frame_paint.h"
#include "components/aerie_frame/aerie_frame_palette.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include <algorithm>
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
  void SetReducedMotion() {
    prefs()->SetString(aerie::motion::kReduceMotionPref,
                       aerie::motion::kReduceMotionOn);
  }
  // Theme and mode through the prefs the Settings page writes, then waits for
  // the frame to show them.
  bool Choose(const char* theme, const char* mode,
              aerie::tokens::ThemeId want_theme,
              aerie::tokens::ColorMode want_mode) {
    prefs()->SetString(aerie::appearance::kThemePref, theme);
    prefs()->SetString(aerie::appearance::kModePref, mode);
    return WaitFor([&] {
      return tint()->inputs_for_testing().theme == want_theme &&
             tint()->inputs_for_testing().mode == want_mode;
    });
  }
  // Minimum contrast of the theme's text, muted text and accent over the
  // painted pixels of the sidebar column on both sides (where #178 samples).
  struct Measured {
    double text = 99, muted = 99, accent = 99;
  };
  Measured MeasurePainted() {
    SkBitmap bitmap;
    tint()->PaintToBitmapForTesting(&bitmap);
    const gfx::Size size = tint()->size();
    const aerie_frame::FramePalette palette =
        aerie_frame::PaletteOf(tint()->inputs_for_testing());
    Measured m;
    auto sample = [&](int x, int y) {
      const SkColor under = bitmap.getColor(std::min(x, size.width() - 1),
                                            std::min(y, size.height() - 1));
      m.text = std::min(m.text, aerie_frame::ContrastRatio(palette.text, under));
      m.muted = std::min(
          m.muted, aerie_frame::ContrastRatio(palette.text_muted, under));
      m.accent =
          std::min(m.accent, aerie_frame::ContrastRatio(palette.accent, under));
    };
    const int sidebar = aerie::tokens::kSizeSidebarWidth;
    for (int y = 0; y <= size.height(); y += 45) {
      for (int x = 0; x <= sidebar; x += 26) {
        sample(x, y);
      }
      for (int x = size.width() - sidebar; x <= size.width(); x += 26) {
        sample(x, y);
      }
    }
    return m;
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

IN_PROC_BROWSER_TEST_F(AerieLookBrowserTest, PaintedContrastMeetsTheMinimumsForEveryThemeAndMode) {
  using aerie::tokens::ColorMode;
  using aerie::tokens::ThemeId;
  SetReducedMotion();
  struct Case {
    const char* id;
    ThemeId theme;
    const char* mode;
    ColorMode color_mode;
  };
  std::vector<Case> cases;
  for (int i = 0; i < aerie::tokens::kThemeCount; ++i) {
    const ThemeId theme = static_cast<ThemeId>(i);
    cases.push_back({aerie::tokens::kThemeIds[i], theme, "dark", ColorMode::kDark});
    if (theme != ThemeId::kOled) {  // OLED has no light table.
      cases.push_back({aerie::tokens::kThemeIds[i], theme, "light", ColorMode::kLight});
    }
  }
  for (const Case& c : cases) {
    SCOPED_TRACE(std::string(c.id) + "/" + c.mode);
    ASSERT_TRUE(Choose(c.id, c.mode, c.theme, c.color_mode));
    const Measured m = MeasurePainted();
    LOG(INFO) << "AERIE_CONTRAST " << c.id << "/" << c.mode << " text=" << m.text
              << " muted=" << m.muted << " accent=" << m.accent;
    const double floor = c.theme == ThemeId::kHighContrast ? 7.0 : 4.5;
    EXPECT_GE(m.text, floor);
    EXPECT_GE(m.muted, floor);
    EXPECT_GE(m.accent, c.theme == ThemeId::kHighContrast ? 7.0 : 3.0);
  }
}

IN_PROC_BROWSER_TEST_F(AerieLookBrowserTest, OledInLightModeIsClean) {
  SetReducedMotion();
  ASSERT_TRUE(Choose("oled", "light", aerie::tokens::ThemeId::kClean,
                     aerie::tokens::ColorMode::kLight));
}

// --- prefers-contrast and forced colours ------------------------------------

// Forces the native theme flags and restores them.
class ScopedNativeThemeFlags {
 public:
  ScopedNativeThemeFlags(views::Widget* widget,
                         ui::NativeTheme::PreferredContrast contrast,
                         ui::ColorProviderKey::ForcedColors forced)
      : themes_{ui::NativeTheme::GetInstanceForNativeUi(),
                widget->GetNativeTheme()} {
    for (ui::NativeTheme* t : themes_) {
      saved_.emplace_back(t->preferred_contrast(), t->forced_colors());
    }
    Set(contrast, forced);
  }
  ~ScopedNativeThemeFlags() {
    for (size_t i = 0; i < themes_.size(); ++i) {
      themes_[i]->set_preferred_contrast(saved_[i].first);
      themes_[i]->set_forced_colors(saved_[i].second);
      themes_[i]->NotifyOnNativeThemeUpdated();
    }
  }
  void Set(ui::NativeTheme::PreferredContrast contrast,
           ui::ColorProviderKey::ForcedColors forced) {
    for (ui::NativeTheme* t : themes_) {
      t->set_preferred_contrast(contrast);
      t->set_forced_colors(forced);
      t->NotifyOnNativeThemeUpdated();
    }
  }

 private:
  std::vector<ui::NativeTheme*> themes_;
  std::vector<std::pair<ui::NativeTheme::PreferredContrast,
                        ui::ColorProviderKey::ForcedColors>>
      saved_;
};

IN_PROC_BROWSER_TEST_F(AerieLookBrowserTest, PrefersContrastMoreSelectsHighContrastUnlessAThemeWasChosen) {
  using aerie::tokens::ThemeId;
  SetReducedMotion();
  ScopedNativeThemeFlags flags(browser_view()->GetWidget(),
                               ui::NativeTheme::PreferredContrast::kMore,
                               ui::ColorProviderKey::ForcedColors::kNone);
  // Never chosen: the OS asks for more contrast, so High contrast.
  ASSERT_TRUE(WaitFor([&] {
    return tint()->inputs_for_testing().theme == ThemeId::kHighContrast;
  }));
  // Chosen, Vivid included: the choice wins.
  prefs()->SetString(aerie::appearance::kThemePref, "vivid");
  ASSERT_TRUE(WaitFor([&] {
    return tint()->inputs_for_testing().theme == ThemeId::kVivid;
  }));
  prefs()->SetString(aerie::appearance::kThemePref, "soft");
  ASSERT_TRUE(WaitFor([&] {
    return tint()->inputs_for_testing().theme == ThemeId::kSoft;
  }));
  // Back to no preference with nothing chosen is Vivid again.
  prefs()->ClearPref(aerie::appearance::kThemePref);
  flags.Set(ui::NativeTheme::PreferredContrast::kNoPreference,
            ui::ColorProviderKey::ForcedColors::kNone);
  ASSERT_TRUE(WaitFor([&] {
    return tint()->inputs_for_testing().theme == ThemeId::kVivid;
  }));
}

IN_PROC_BROWSER_TEST_F(AerieLookBrowserTest, ForcedColoursPaintWithSystemColoursAndNoAerieTint) {
  using aerie::tokens::ThemeId;
  SetReducedMotion();
  prefs()->SetString(aerie::appearance::kThemePref, "soft");
  ASSERT_TRUE(WaitFor([&] {
    return tint()->inputs_for_testing().theme == ThemeId::kSoft;
  }));
  const aerie_frame::FramePalette tinted =
      aerie_frame::PaletteOf(tint()->inputs_for_testing());
  EXPECT_GT(tinted.stop_count, 1);
  ScopedNativeThemeFlags flags(browser_view()->GetWidget(),
                               ui::NativeTheme::PreferredContrast::kNoPreference,
                               ui::ColorProviderKey::ForcedColors::kActive);
  ASSERT_TRUE(WaitFor([&] {
    return tint()->inputs_for_testing().system_colours.has_value();
  }));
  const aerie_frame::FramePalette forced =
      aerie_frame::PaletteOf(tint()->inputs_for_testing());
  // One flat colour, no gradient stops: the system's, not the theme's.
  EXPECT_EQ(forced.stop_count, 1);
  const auto sys = *tint()->inputs_for_testing().system_colours;
  EXPECT_EQ(forced.frame_base, sys.background);
  EXPECT_EQ(forced.text, sys.text);
  // The whole painted frame is that one colour outside the card and its border.
  SkBitmap bitmap;
  tint()->PaintToBitmapForTesting(&bitmap);
  EXPECT_EQ(bitmap.getColor(2, bitmap.height() - 2), sys.background);
  EXPECT_EQ(bitmap.getColor(bitmap.width() - 2, 2), sys.background);
  // And back to the tint when forced colours go.
  flags.Set(ui::NativeTheme::PreferredContrast::kNoPreference,
            ui::ColorProviderKey::ForcedColors::kNone);
  ASSERT_TRUE(WaitFor([&] {
    return !tint()->inputs_for_testing().system_colours.has_value();
  }));
}

// --- Restart ----------------------------------------------------------------

IN_PROC_BROWSER_TEST_F(AerieLookBrowserTest, PRE_ThemeAndModeSurviveARestart) {
  SetReducedMotion();
  ASSERT_TRUE(Choose("oled", "dark", aerie::tokens::ThemeId::kOled,
                     aerie::tokens::ColorMode::kDark));
}

IN_PROC_BROWSER_TEST_F(AerieLookBrowserTest, ThemeAndModeSurviveARestart) {
  EXPECT_EQ(prefs()->GetString(aerie::appearance::kThemePref), "oled");
  EXPECT_EQ(prefs()->GetString(aerie::appearance::kModePref), "dark");
  ASSERT_TRUE(WaitFor([&] {
    return tint()->inputs_for_testing().theme == aerie::tokens::ThemeId::kOled &&
           tint()->inputs_for_testing().mode == aerie::tokens::ColorMode::kDark;
  }));
  EXPECT_EQ(ThemeServiceFactory::GetForProfile(browser()->GetProfile())
                ->GetBrowserColorScheme(),
            ThemeService::BrowserColorScheme::kDark);
  // Applied at launch: no fade.
  EXPECT_FALSE(fade().running());
  EXPECT_EQ(fade().finished_count(), 0);
}

}  // namespace
