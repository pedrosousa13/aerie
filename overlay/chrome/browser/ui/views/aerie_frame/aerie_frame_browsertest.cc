// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

// Browser tests of the Aerie window frame (issue #178): the tint is the lowest
// layer of a normal window and follows the Workspace record in the Tab Model;
// the pixels behind the sidebar text keep the contrast the design asks for
// (the numbers are the output of check_contrast.py, and
// tests/test_frame_contrast_golden.py checks that they still are); it paints
// once and again only when its state changes; nothing about it or the sidebar
// is animated, and reduced motion changes none of this because there is
// nothing to animate; native frame materials are off.

#include <algorithm>
#include <string>

#include "base/functional/bind.h"
#include "base/run_loop.h"
#include "base/task/single_thread_task_runner.h"
#include "base/time/time.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/aerie_tab_model/tab_model_service.h"
#include "chrome/browser/aerie_tab_model/tab_model_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/views/aerie_frame/frame_tint_view.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/contents_container_view.h"
#include "chrome/browser/ui/views/frame/multi_contents_view.h"
#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"
#include "chrome/browser/ui/views/helium/frame_corner_radius.h"
#include "chrome/browser/ui/views/helium/native_frame_materials.h"
#include "chrome/common/pref_names.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "components/aerie_design_tokens/aerie_tokens.h"
#include "components/aerie_frame/aerie_frame_palette.h"
#include "components/aerie_sidebar/aerie_sidebar_layout.h"
#include "components/prefs/pref_service.h"
#include "content/public/test/browser_test.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/color/color_provider_key.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/layer_animator.h"
#include "ui/gfx/animation/animation_test_api.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace {

using aerie::tokens::ColorMode;
using aerie::tokens::ThemeId;

// The window the checker models.
constexpr gfx::Size kCheckerWindow(1440, 900);

// The minimum contrast of the sidebar text, muted text and accent over the
// sidebar column of a 1440 x 900 Vivid and Dusk frame, as check_contrast.py -v
// prints them for "vivid/<mode>/dusk". The painted pixels have to land within
// kTolerance of these.
struct CheckerMinimums {
  double text;
  double muted;
  double accent;
};
constexpr CheckerMinimums kCheckerLight = {6.22, 5.46, 3.08};
constexpr CheckerMinimums kCheckerDark = {6.29, 5.51, 3.24};
constexpr double kTolerance = 0.06;

// The design's floors: text and muted text 4.5:1, accent 3:1.
constexpr double kTextFloor = 4.5;
constexpr double kAccentFloor = 3.0;

// Runs `visit` on every layer of the views below and including `root`.
template <typename Visit>
void ForEachLayer(views::View* root, const Visit& visit) {
  if (root->layer()) {
    visit(root->layer());
  }
  for (views::View* child : root->children()) {
    ForEachLayer(child, visit);
  }
}

}  // namespace

class AerieFrameBrowserTest : public InProcessBrowserTest {
 protected:
  BrowserView* browser_view() {
    return BrowserView::GetBrowserViewForBrowser(browser());
  }
  FrameTintView* tint() { return browser_view()->frame_tint_view_for_testing(); }
  PrefService* prefs() { return browser()->GetProfile()->GetPrefs(); }
  aerie_tab_model::TabModel* model() {
    return aerie_tab_model::TabModelServiceFactory::GetForProfile(
               browser()->GetProfile())
        ->model();
  }

  // Runs the message loop until `done` is true, and fails after 10 s of loop.
  template <typename Predicate>
  bool WaitFor(Predicate done) {
    const base::TimeTicks deadline = base::TimeTicks::Now() + base::Seconds(10);
    while (!done() && base::TimeTicks::Now() < deadline) {
      Pump(base::Milliseconds(20));
    }
    return done();
  }

  void Pump(base::TimeDelta duration) {
    base::RunLoop loop;
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, loop.QuitClosure(), duration);
    loop.Run();
  }

  void SetColour(const std::string& colour) {
    model()->UpdateWorkspace(
        [&](aerie_tab_model::Workspace& workspace) { workspace.colour = colour; });
  }

  void SetMode(ColorMode mode) {
    browser_view()->GetWidget()->SetColorModeOverride(
        mode == ColorMode::kDark ? ui::ColorProviderKey::ColorMode::kDark
                                 : ui::ColorProviderKey::ColorMode::kLight);
  }

  // Makes the tint 1440 x 900, the window the checker models, whatever the
  // frame around the BrowserView takes.
  void ResizeTintTo(const gfx::Size& size) {
    gfx::Rect bounds = browser()->window()->GetBounds();
    for (int attempt = 0; attempt < 3 && tint()->size() != size; ++attempt) {
      bounds.set_width(bounds.width() + size.width() - tint()->width());
      bounds.set_height(bounds.height() + size.height() - tint()->height());
      browser()->window()->SetBounds(bounds);
      WaitFor([&] { return tint()->size() == size; });
    }
  }

  // The minimum contrast of the painted pixels over the sidebar column on
  // both sides of the window, sampled where check_contrast.py samples.
  struct Measured {
    double text = 99;
    double muted = 99;
    double accent = 99;
  };
  Measured MeasurePaintedContrast(ColorMode mode, const std::string& colour) {
    SkBitmap bitmap;
    tint()->PaintToBitmapForTesting(&bitmap);
    const gfx::Size size = tint()->size();
    const aerie_frame::FramePalette palette =
        aerie_frame::ResolveFramePalette(ThemeId::kVivid, mode, colour);
    Measured m;
    auto sample = [&](int x, int y) {
      const SkColor under = bitmap.getColor(std::min(x, size.width() - 1),
                                            std::min(y, size.height() - 1));
      m.text = std::min(m.text, aerie_frame::ContrastRatio(palette.text, under));
      m.muted =
          std::min(m.muted, aerie_frame::ContrastRatio(palette.text_muted, under));
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

  // A hash of the painted frame, to tell whether a state changed what it shows.
  uint64_t PaintedHash() {
    SkBitmap bitmap;
    tint()->PaintToBitmapForTesting(&bitmap);
    uint64_t hash = 1469598103934665603ull;
    for (int y = 0; y < bitmap.height(); y += 7) {
      for (int x = 0; x < bitmap.width(); x += 7) {
        hash = (hash ^ bitmap.getColor(x, y)) * 1099511628211ull;
      }
    }
    return hash;
  }
};

// --- What the window is -------------------------------------------------

IN_PROC_BROWSER_TEST_F(AerieFrameBrowserTest, TheTintIsTheLowestLayerAndFillsTheWindow) {
  ASSERT_TRUE(tint());
  EXPECT_EQ(tint()->parent(), browser_view());
  EXPECT_EQ(browser_view()->GetIndexOf(tint()), std::optional<size_t>(0));
  EXPECT_EQ(tint()->bounds(), browser_view()->GetLocalBounds());
  ASSERT_TRUE(tint()->layer());
  EXPECT_EQ(tint()->layer()->type(), ui::LAYER_TEXTURED);
  EXPECT_TRUE(tint()->layer()->fills_bounds_opaquely());
}

IN_PROC_BROWSER_TEST_F(AerieFrameBrowserTest, AFreshProfileShowsVividDuskAndARoundedCardWithTokenRadius) {
  ASSERT_TRUE(tint());
  EXPECT_TRUE(aerie_sidebar::IsSidebarLayout(*prefs()));
  EXPECT_EQ(tint()->inputs_for_testing().theme, ThemeId::kVivid);
  EXPECT_EQ(tint()->inputs_for_testing().colour, "dusk");
  EXPECT_FALSE(tint()->inputs_for_testing().card.IsEmpty());

  // The card is rounded with radius-xl, even though the Helium pref is off.
  prefs()->SetBoolean(prefs::kHeliumRoundedFrame, false);
  browser_view()->multi_contents_view()->InvalidateLayout();
  EXPECT_EQ(GetGenericFrameRadius(), aerie::tokens::kRadiusXl);
  EXPECT_EQ(GetPaddedFrameRadius(), aerie::tokens::kRadiusXl);
  ContentsContainerView* card =
      browser_view()->multi_contents_view()->GetActiveContentsContainerView();
  ASSERT_TRUE(card);
  // A rounded card reserves its outline's stroke in a border; a square one has none.
  EXPECT_TRUE(card->GetBorder());
}

IN_PROC_BROWSER_TEST_F(AerieFrameBrowserTest, NativeFrameMaterialsAreOffEvenWhenTheirPrefIsOn) {
  prefs()->SetBoolean(prefs::kHeliumNativeFrameMaterials, true);
  EXPECT_FALSE(helium::ShouldUseNativeFrameMaterials(browser_view()));
  EXPECT_FALSE(helium::ShouldUseNativeFrameMaterials(browser()));
}

// --- The Workspace record -----------------------------------------------

IN_PROC_BROWSER_TEST_F(AerieFrameBrowserTest, TheFrameFollowsTheWorkspaceRecord) {
  ASSERT_TRUE(tint());
  ASSERT_TRUE(WaitFor([&] { return tint()->paint_count() >= 1; }));
  const uint64_t dusk = PaintedHash();

  SetColour("tide");
  EXPECT_TRUE(WaitFor([&] { return tint()->inputs_for_testing().colour == "tide"; }));
  EXPECT_NE(PaintedHash(), dusk);

  // And the repaint really ran: the layer was painted again.
  const int before = tint()->paint_count();
  SetColour("moss");
  EXPECT_TRUE(WaitFor([&] { return tint()->paint_count() > before; }));
  EXPECT_EQ(tint()->inputs_for_testing().colour, "moss");

  // A custom colour from the editor (M5) is one stop.
  SetColour("#336699");
  EXPECT_TRUE(WaitFor([&] { return tint()->inputs_for_testing().colour == "#336699"; }));
}

// --- Contrast of the painted pixels -------------------------------------

IN_PROC_BROWSER_TEST_F(AerieFrameBrowserTest, PaintedContrastMatchesTheCheckerInLightAndDark) {
  ASSERT_TRUE(tint());
  ResizeTintTo(kCheckerWindow);
  ASSERT_EQ(tint()->size(), kCheckerWindow);

  struct Case {
    ColorMode mode;
    CheckerMinimums checker;
  };
  for (const Case& c : {Case{ColorMode::kLight, kCheckerLight},
                        Case{ColorMode::kDark, kCheckerDark}}) {
    SetMode(c.mode);
    ASSERT_TRUE(WaitFor([&] { return tint()->inputs_for_testing().mode == c.mode; }));
    const Measured m = MeasurePaintedContrast(c.mode, "dusk");
    EXPECT_NEAR(m.text, c.checker.text, kTolerance);
    EXPECT_NEAR(m.muted, c.checker.muted, kTolerance);
    EXPECT_NEAR(m.accent, c.checker.accent, kTolerance);
    EXPECT_GE(m.text, kTextFloor);
    EXPECT_GE(m.muted, kTextFloor);
    EXPECT_GE(m.accent, kAccentFloor);
  }
}

IN_PROC_BROWSER_TEST_F(AerieFrameBrowserTest, EveryPresetKeepsTheMinimumsOnTheRealPaint) {
  ASSERT_TRUE(tint());
  ResizeTintTo(kCheckerWindow);
  for (ColorMode mode : {ColorMode::kLight, ColorMode::kDark}) {
    SetMode(mode);
    for (const char* name : aerie::tokens::kWorkspacePresetIds) {
      SetColour(name);
      ASSERT_TRUE(WaitFor([&] {
        return tint()->inputs_for_testing().colour == name &&
               tint()->inputs_for_testing().mode == mode;
      }));
      const Measured m = MeasurePaintedContrast(mode, name);
      EXPECT_GE(m.text, kTextFloor) << name;
      EXPECT_GE(m.muted, kTextFloor) << name;
      EXPECT_GE(m.accent, kAccentFloor) << name;
    }
  }
}

// --- Paint once ----------------------------------------------------------

IN_PROC_BROWSER_TEST_F(AerieFrameBrowserTest, ThePaintIsOnceAndOnStateChangeOnly) {
  ASSERT_TRUE(tint());
  ASSERT_TRUE(WaitFor([&] { return tint()->paint_count() >= 1; }));
  // Let the window settle, then watch a stretch of frames: nothing repaints.
  Pump(base::Milliseconds(300));
  const int settled = tint()->paint_count();
  Pump(base::Milliseconds(500));
  EXPECT_EQ(tint()->paint_count(), settled);

  // A Workspace colour change paints once.
  SetColour("ember");
  ASSERT_TRUE(WaitFor([&] { return tint()->paint_count() > settled; }));
  Pump(base::Milliseconds(300));
  const int after_colour = tint()->paint_count();
  EXPECT_EQ(after_colour, settled + 1);

  // So does a mode change.
  SetMode(ColorMode::kDark);
  ASSERT_TRUE(WaitFor([&] { return tint()->paint_count() > after_colour; }));
  Pump(base::Milliseconds(300));
  EXPECT_EQ(tint()->paint_count(), after_colour + 1);
}

// --- Not animated ---------------------------------------------------------

IN_PROC_BROWSER_TEST_F(AerieFrameBrowserTest, NothingOnTheTintIsAnimated) {
  ASSERT_TRUE(tint());
  ui::Layer* layer = tint()->layer();
  auto expect_still = [&] {
    ui::LayerAnimator* animator = layer->GetAnimator();
    EXPECT_FALSE(animator->is_animating());
    for (auto property :
         {ui::LayerAnimationElement::TRANSFORM, ui::LayerAnimationElement::BOUNDS,
          ui::LayerAnimationElement::OPACITY, ui::LayerAnimationElement::VISIBILITY,
          ui::LayerAnimationElement::BRIGHTNESS, ui::LayerAnimationElement::GRAYSCALE,
          ui::LayerAnimationElement::COLOR, ui::LayerAnimationElement::CLIP,
          ui::LayerAnimationElement::ROUNDED_CORNERS,
          ui::LayerAnimationElement::GRADIENT_MASK}) {
      EXPECT_FALSE(animator->IsAnimatingProperty(property));
    }
    EXPECT_EQ(layer->background_blur(), 0);
    EXPECT_EQ(layer->layer_blur(), 0);
    EXPECT_TRUE(layer->GetTargetTransform().IsIdentity());
    EXPECT_EQ(layer->GetTargetOpacity(), 1.0f);
  };
  expect_still();
  SetColour("lilac");
  WaitFor([&] { return tint()->inputs_for_testing().colour == "lilac"; });
  expect_still();
  SetMode(ColorMode::kDark);
  WaitFor([&] { return tint()->inputs_for_testing().mode == ColorMode::kDark; });
  expect_still();
  chrome::ExecuteCommand(browser(), IDC_CTRL_S_SHORTCUT);
  expect_still();
}

IN_PROC_BROWSER_TEST_F(AerieFrameBrowserTest, NoFilterBlurOrShadowIsAnimatedAnywhereInTheSidebar) {
  VerticalTabStripRegionView* region =
      browser_view()->vertical_tab_strip_region_view_for_testing();
  ASSERT_TRUE(region);
  auto check = [&] {
    ForEachLayer(region, [](ui::Layer* layer) {
      ui::LayerAnimator* animator = layer->GetAnimator();
      // Only the slide may animate, and only its transform and opacity.
      for (auto property :
           {ui::LayerAnimationElement::BRIGHTNESS,
            ui::LayerAnimationElement::GRAYSCALE, ui::LayerAnimationElement::COLOR,
            ui::LayerAnimationElement::CLIP, ui::LayerAnimationElement::ROUNDED_CORNERS,
            ui::LayerAnimationElement::GRADIENT_MASK}) {
        EXPECT_FALSE(animator->IsAnimatingProperty(property));
      }
      EXPECT_EQ(layer->background_blur(), 0);
      EXPECT_EQ(layer->layer_blur(), 0);
    });
  };
  check();
  auto full_motion = gfx::AnimationTestApi::SetPrefersReducedMotionForTesting(false);
  chrome::ExecuteCommand(browser(), IDC_CTRL_S_SHORTCUT);
  check();  // During the slide.
  WaitFor([&] { return region->aerie_sidebar_hidden(); });
  check();
  chrome::ExecuteCommand(browser(), IDC_CTRL_S_SHORTCUT);
  check();
}

IN_PROC_BROWSER_TEST_F(AerieFrameBrowserTest, ReducedMotionChangesNothingHereBecauseThereIsNothingToAnimate) {
  ASSERT_TRUE(tint());
  ASSERT_TRUE(WaitFor([&] { return tint()->paint_count() >= 1; }));
  Pump(base::Milliseconds(300));
  uint64_t hashes[2];
  int paints[2];
  int index = 0;
  for (bool reduced : {false, true}) {
    auto scope = gfx::AnimationTestApi::SetPrefersReducedMotionForTesting(reduced);
    SetColour("dusk");
    SetMode(ColorMode::kLight);
    Pump(base::Milliseconds(300));
    const int before = tint()->paint_count();
    SetColour("tide");
    WaitFor([&] { return tint()->paint_count() > before; });
    Pump(base::Milliseconds(300));
    hashes[index] = PaintedHash();
    paints[index] = tint()->paint_count() - before;
    EXPECT_TRUE(tint()->layer()->GetTargetTransform().IsIdentity());
    EXPECT_FALSE(tint()->layer()->GetAnimator()->is_animating());
    ++index;
  }
  // The same paint, the same number of paints, with and without reduced motion.
  EXPECT_EQ(hashes[0], hashes[1]);
  EXPECT_EQ(paints[0], paints[1]);
}

// --- Window states --------------------------------------------------------

IN_PROC_BROWSER_TEST_F(AerieFrameBrowserTest, TheTintFollowsTheWindowAtASmallSizeAndMaximized) {
  ASSERT_TRUE(tint());
  // Below 1000 px.
  ResizeTintTo(gfx::Size(900, 700));
  EXPECT_EQ(tint()->size(), gfx::Size(900, 700));
  EXPECT_EQ(tint()->bounds(), browser_view()->GetLocalBounds());
  EXPECT_FALSE(tint()->inputs_for_testing().card.IsEmpty());
  const Measured small = MeasurePaintedContrast(ColorMode::kLight, "dusk");
  EXPECT_GE(small.text, kTextFloor);
  EXPECT_GE(small.muted, kTextFloor);
  EXPECT_GE(small.accent, kAccentFloor);

  browser()->window()->Maximize();
  ASSERT_TRUE(WaitFor([&] { return browser()->window()->IsMaximized(); }));
  ASSERT_TRUE(WaitFor([&] { return tint()->bounds() == browser_view()->GetLocalBounds(); }));
  EXPECT_FALSE(tint()->inputs_for_testing().card.IsEmpty());
  browser()->window()->Restore();
}

IN_PROC_BROWSER_TEST_F(AerieFrameBrowserTest, AnInactiveWindowPaintsTheSameFrame) {
  ASSERT_TRUE(tint());
  ASSERT_TRUE(WaitFor([&] { return tint()->paint_count() >= 1; }));
  Pump(base::Milliseconds(300));
  const uint64_t active = PaintedHash();
  const int paints = tint()->paint_count();
  browser()->window()->Deactivate();
  Pump(base::Milliseconds(300));
  EXPECT_EQ(PaintedHash(), active);
  EXPECT_EQ(tint()->paint_count(), paints);
}

IN_PROC_BROWSER_TEST_F(AerieFrameBrowserTest, AFullscreenWindowStillHasTheTintAtTheWindowSize) {
  ASSERT_TRUE(tint());
  chrome::ToggleFullscreenMode(browser());
  ASSERT_TRUE(WaitFor([&] { return browser()->window()->IsFullscreen(); }));
  ASSERT_TRUE(WaitFor([&] { return tint()->bounds() == browser_view()->GetLocalBounds(); }));
  chrome::ToggleFullscreenMode(browser());
  ASSERT_TRUE(WaitFor([&] { return !browser()->window()->IsFullscreen(); }));
  EXPECT_EQ(tint()->bounds(), browser_view()->GetLocalBounds());
}
