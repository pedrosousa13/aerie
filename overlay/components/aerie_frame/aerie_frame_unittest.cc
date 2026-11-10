// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

// Tests of the window frame's palette, its paint and its repaint gate (issue
// #178). The golden numbers are the output of check_contrast.py's frame model
// for Vivid and Dusk (tests/test_frame_contrast_golden.py checks that they
// still are), so the C++ paint and the Python model cannot drift apart.

#include <algorithm>
#include <cmath>
#include <vector>

#include "base/memory/discardable_memory_allocator.h"
#include "base/test/test_discardable_memory_allocator.h"
#include "components/aerie_design_tokens/aerie_tokens.h"
#include "components/aerie_frame/aerie_frame_enabled.h"
#include "components/aerie_frame/aerie_frame_paint.h"
#include "components/aerie_frame/aerie_frame_palette.h"
#include "components/aerie_sidebar/aerie_sidebar_prefs.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/testing_pref_service.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/gfx/canvas.h"

namespace aerie_frame {
namespace {

using aerie::tokens::ColorMode;
using aerie::tokens::ThemeId;

constexpr gfx::Size kWindow(1440, 900);
constexpr int kSidebarWidth = aerie::tokens::kSizeSidebarWidth;

// The sample grid of check_contrast.py: the sidebar column on either side of
// the window, every 26 px across and every 45 px down.
std::vector<gfx::PointF> SidebarSamples(const gfx::Size& size) {
  std::vector<gfx::PointF> points;
  for (int y = 0; y <= size.height(); y += 45) {
    for (int x = 0; x <= kSidebarWidth; x += 26) {
      points.emplace_back(x, y);
    }
    for (int x = size.width() - kSidebarWidth; x <= size.width(); x += 26) {
      points.emplace_back(x, y);
    }
  }
  return points;
}

struct Minimums {
  double text = 99;
  double muted = 99;
  double accent = 99;
};

Minimums MinimumContrast(const FramePalette& palette, const gfx::Size& size) {
  Minimums min;
  for (const gfx::PointF& p : SidebarSamples(size)) {
    const SkColor under = FrameColorAt(palette, size, false, p);
    min.text = std::min(min.text, ContrastRatio(palette.text, under));
    min.muted = std::min(min.muted, ContrastRatio(palette.text_muted, under));
    min.accent = std::min(min.accent, ContrastRatio(palette.accent, under));
  }
  return min;
}

void ExpectColorNear(SkColor actual, int r, int g, int b, int tolerance) {
  EXPECT_NEAR(SkColorGetR(actual), r, tolerance);
  EXPECT_NEAR(SkColorGetG(actual), g, tolerance);
  EXPECT_NEAR(SkColorGetB(actual), b, tolerance);
}

// --- Palette -----------------------------------------------------------

TEST(AerieFramePaletteTest, StopsAreTheWorkspaceStopsMixedTowardTheBase) {
  const FramePalette p =
      ResolveFramePalette(ThemeId::kVivid, ColorMode::kLight, "dusk");
  const auto& tokens =
      aerie::tokens::GetThemeTokens(ThemeId::kVivid, ColorMode::kLight);
  EXPECT_EQ(p.frame_base, tokens.color_frame_base);
  EXPECT_EQ(p.stop_count, 3);
  EXPECT_FLOAT_EQ(tokens.theme_tint_strength, 0.60f);
  // First dusk stop is #FFA463; 0.60 of it over the base #ECEBF0.
  ExpectColorNear(p.stops[0], std::lround(0.6 * 0xFF + 0.4 * 0xEC),
                  std::lround(0.6 * 0xA4 + 0.4 * 0xEB),
                  std::lround(0.6 * 0x63 + 0.4 * 0xF0), 1);
  EXPECT_EQ(p.angle_degrees, aerie::tokens::kTintAngle);
}

TEST(AerieFramePaletteTest, VividTintStrengthIsSixtyLightAndFiftyDark) {
  EXPECT_FLOAT_EQ(aerie::tokens::GetThemeTokens(ThemeId::kVivid,
                                                ColorMode::kLight)
                      .theme_tint_strength,
                  0.60f);
  EXPECT_FLOAT_EQ(aerie::tokens::GetThemeTokens(ThemeId::kVivid,
                                                ColorMode::kDark)
                      .theme_tint_strength,
                  0.50f);
}

TEST(AerieFramePaletteTest, AnUnknownColourIsDusk) {
  const FramePalette dusk =
      ResolveFramePalette(ThemeId::kVivid, ColorMode::kDark, "dusk");
  for (const char* name : {"", "teal", "#12", "#GGGGGG", "default"}) {
    const FramePalette p =
        ResolveFramePalette(ThemeId::kVivid, ColorMode::kDark, name);
    EXPECT_EQ(p.stops, dusk.stops) << name;
    EXPECT_EQ(p.accent, dusk.accent) << name;
  }
}

TEST(AerieFramePaletteTest, EveryPresetHasItsOwnFrame) {
  FramePalette previous =
      ResolveFramePalette(ThemeId::kVivid, ColorMode::kLight, "dusk");
  for (int i = 1; i < aerie::tokens::kWorkspacePresetCount; ++i) {
    const FramePalette p = ResolveFramePalette(
        ThemeId::kVivid, ColorMode::kLight,
        aerie::tokens::kWorkspacePresetIds[i]);
    EXPECT_NE(p.stops[0], previous.stops[0])
        << aerie::tokens::kWorkspacePresetIds[i];
    previous = p;
  }
}

TEST(AerieFramePaletteTest, ACustomColourIsOneStopAndTheAccentHue) {
  const FramePalette p =
      ResolveFramePalette(ThemeId::kVivid, ColorMode::kLight, "#336699");
  EXPECT_EQ(p.stop_count, 1);
  // Single stop: the whole frame is that colour at the tint strength.
  const SkColor a = FrameColorAt(p, kWindow, false, gfx::PointF(10, 10));
  const SkColor b = FrameColorAt(p, kWindow, false, gfx::PointF(1300, 800));
  // The glow lights the first corner only.
  EXPECT_GT(SkColorGetR(a), SkColorGetR(b));
  ExpectColorNear(b, std::lround(0.6 * 0x33 + 0.4 * 0xEC),
                  std::lround(0.6 * 0x66 + 0.4 * 0xEB),
                  std::lround(0.6 * 0x99 + 0.4 * 0xF0), 3);
}

TEST(AerieFramePaletteTest, VividAsksForNoCardBorderInEitherMode) {
  EXPECT_EQ(CardBorderWidth(ThemeId::kVivid, ColorMode::kLight), 0);
  EXPECT_EQ(CardBorderWidth(ThemeId::kVivid, ColorMode::kDark), 0);
  EXPECT_EQ(CardBorderWidth(ThemeId::kHighContrast, ColorMode::kLight), 2);
}

TEST(AerieFramePaletteTest, TheCardComesFromTheTokens) {
  const FramePalette p =
      ResolveFramePalette(ThemeId::kVivid, ColorMode::kLight, "dusk");
  EXPECT_EQ(p.card_radius, aerie::tokens::kRadiusXl);
  EXPECT_EQ(p.card_radius, 14);
  EXPECT_EQ(p.card_surface, aerie::tokens::kColorContentSurfaceLight);
  EXPECT_EQ(p.card_shadow.count, aerie::tokens::kShadowCardLight.count);
  EXPECT_EQ(p.card_shadow.layers[0].blur,
            aerie::tokens::kShadowCardLight.layers[0].blur);
  // Vivid asks for no card border.
  EXPECT_EQ(p.card_border_width, 0);
  // High contrast asks for one.
  EXPECT_GT(ResolveFramePalette(ThemeId::kHighContrast, ColorMode::kLight, "dusk")
                .card_border_width,
            0);
}

// --- The model of the paint, against check_contrast.py -----------------

TEST(AerieFrameModelTest, MatchesTheCheckerAtFivePointsLight) {
  const FramePalette p =
      ResolveFramePalette(ThemeId::kVivid, ColorMode::kLight, "dusk");
  auto at = [&](float x, float y) {
    return FrameColorAt(p, kWindow, false, gfx::PointF(x, y));
  };
  ExpectColorNear(at(0, 0), 249, 205, 175, 2);
  ExpectColorNear(at(130, 450), 243, 164, 159, 2);
  ExpectColorNear(at(720, 450), 238, 136, 163, 2);
  ExpectColorNear(at(1440, 900), 168, 132, 233, 2);
  ExpectColorNear(at(1300, 100), 225, 135, 176, 2);
}

TEST(AerieFrameModelTest, MatchesTheCheckerAtFivePointsDark) {
  const FramePalette p =
      ResolveFramePalette(ThemeId::kVivid, ColorMode::kDark, "dusk");
  auto at = [&](float x, float y) {
    return FrameColorAt(p, kWindow, false, gfx::PointF(x, y));
  };
  ExpectColorNear(at(0, 0), 117, 84, 62, 2);
  ExpectColorNear(at(130, 450), 106, 54, 52, 2);
  ExpectColorNear(at(720, 450), 103, 36, 54, 2);
  ExpectColorNear(at(1440, 900), 57, 33, 100, 2);
  ExpectColorNear(at(1300, 100), 94, 35, 63, 2);
}

TEST(AerieFrameModelTest, MirroringPutsTheGlowAtTheOtherCorner) {
  const FramePalette p =
      ResolveFramePalette(ThemeId::kVivid, ColorMode::kLight, "dusk");
  EXPECT_EQ(FrameColorAt(p, kWindow, false, gfx::PointF(0, 0)),
            FrameColorAt(p, kWindow, true, gfx::PointF(1440, 0)));
}

TEST(AerieFrameModelTest, WithTheSidebarOnTheRightTheGlowIsAtTheTopRightNotTheTopLeft) {
  const FramePalette p =
      ResolveFramePalette(ThemeId::kVivid, ColorMode::kLight, "#336699");
  ASSERT_GT(p.glow, 0);
  const SkColor top_left_on_left =
      FrameColorAt(p, kWindow, false, gfx::PointF(10, 10));
  const SkColor top_right_on_right =
      FrameColorAt(p, kWindow, true, gfx::PointF(1430, 10));
  const SkColor top_left_on_right =
      FrameColorAt(p, kWindow, true, gfx::PointF(10, 10));
  const SkColor top_right_on_left =
      FrameColorAt(p, kWindow, false, gfx::PointF(1430, 10));
  EXPECT_EQ(top_left_on_left, top_right_on_right);
  EXPECT_EQ(top_left_on_right, top_right_on_left);
  // The corner nearest the sidebar is the lit one in both placements.
  EXPECT_GT(SkColorGetR(top_right_on_right), SkColorGetR(top_left_on_right));
  EXPECT_GT(SkColorGetR(top_left_on_left), SkColorGetR(top_right_on_left));
}

// --- Contrast on the frame ---------------------------------------------

TEST(AerieFrameContrastTest, VividDuskLightMatchesTheCheckerAndMeetsTheMinimums) {
  const Minimums m = MinimumContrast(
      ResolveFramePalette(ThemeId::kVivid, ColorMode::kLight, "dusk"), kWindow);
  EXPECT_NEAR(m.text, 6.22, 0.05);
  EXPECT_NEAR(m.muted, 5.46, 0.05);
  EXPECT_NEAR(m.accent, 3.08, 0.05);
  EXPECT_GE(m.text, 4.5);
  EXPECT_GE(m.muted, 4.5);
  EXPECT_GE(m.accent, 3.0);
}

TEST(AerieFrameContrastTest, VividDuskDarkMatchesTheCheckerAndMeetsTheMinimums) {
  const Minimums m = MinimumContrast(
      ResolveFramePalette(ThemeId::kVivid, ColorMode::kDark, "dusk"), kWindow);
  EXPECT_NEAR(m.text, 6.29, 0.05);
  EXPECT_NEAR(m.muted, 5.51, 0.05);
  EXPECT_NEAR(m.accent, 3.24, 0.05);
  EXPECT_GE(m.text, 4.5);
  EXPECT_GE(m.muted, 4.5);
  EXPECT_GE(m.accent, 3.0);
}

TEST(AerieFrameContrastTest, AWindowBelowOneThousandPixelsKeepsTheMinimums) {
  for (ColorMode mode : {ColorMode::kLight, ColorMode::kDark}) {
    const Minimums m = MinimumContrast(
        ResolveFramePalette(ThemeId::kVivid, mode, "dusk"), gfx::Size(900, 700));
    EXPECT_GE(m.text, 4.5);
    EXPECT_GE(m.muted, 4.5);
    EXPECT_GE(m.accent, 3.0);
  }
}

TEST(AerieFrameContrastTest, EveryPresetKeepsTheMinimumsInBothModes) {
  for (ColorMode mode : {ColorMode::kLight, ColorMode::kDark}) {
    for (const char* name : aerie::tokens::kWorkspacePresetIds) {
      const Minimums m = MinimumContrast(
          ResolveFramePalette(ThemeId::kVivid, mode, name), kWindow);
      EXPECT_GE(m.text, 4.5) << name;
      EXPECT_GE(m.muted, 4.5) << name;
      EXPECT_GE(m.accent, 3.0) << name;
    }
  }
}

// The address pill's field is translucent, so it reads on the frame only if
// the text on field-over-frame does. This is the MODEL: it reads the Guard icon
// as the theme's muted text, which is what the token design intends. The colour
// ids that are really drawn today (ui::kColorIcon, kColorOmniboxIconForeground
// on kColorLocationBarBackground) are measured by the browser test
// ThePillAndTheGuardIconReadOnThePaintedFrameWithTheRealColourIds.
TEST(AerieFrameContrastTest, ThePillFieldAndTheGuardIconReadOnTheTint) {
  for (ColorMode mode : {ColorMode::kLight, ColorMode::kDark}) {
    const FramePalette p = ResolveFramePalette(ThemeId::kVivid, mode, "dusk");
    double min_text = 99;
    double min_icon = 99;
    for (const gfx::PointF& pt : SidebarSamples(kWindow)) {
      const SkColor field =
          Over(p.field, FrameColorAt(p, kWindow, false, pt));
      min_text = std::min(min_text, ContrastRatio(p.text, field));
      min_icon = std::min(min_icon, ContrastRatio(p.text_muted, field));
    }
    EXPECT_GE(min_text, 4.5);
    EXPECT_GE(min_icon, 3.0);
  }
}

// --- The paint ----------------------------------------------------------

// Skia caches gradient data in discardable memory, which a unit test has to
// provide (the browser has the real allocator).
class AerieFramePaintTest : public testing::Test {
 public:
  AerieFramePaintTest() {
    base::DiscardableMemoryAllocator::SetInstance(&allocator_);
  }
  ~AerieFramePaintTest() override {
    base::DiscardableMemoryAllocator::SetInstance(nullptr);
  }

 private:
  base::TestDiscardableMemoryAllocator allocator_;
};

TEST_F(AerieFramePaintTest, PaintedPixelsEqualTheModel) {
  for (ColorMode mode : {ColorMode::kLight, ColorMode::kDark}) {
    const FramePalette p = ResolveFramePalette(ThemeId::kVivid, mode, "dusk");
    gfx::Canvas canvas(kWindow, 1.0f, /*is_opaque=*/true);
    // The card leaves the two sidebar columns bare (the right one is where a
    // sidebar on the right would be).
    PaintFrame(&canvas, p, kWindow, false,
               gfx::Rect(kSidebarWidth + 8, 8, 904, 884));
    const SkBitmap bitmap = canvas.GetBitmap();
    for (const gfx::PointF& pt : SidebarSamples(kWindow)) {
      const int x = std::min<int>(pt.x(), kWindow.width() - 1);
      const int y = std::min<int>(pt.y(), kWindow.height() - 1);
      const SkColor model =
          FrameColorAt(p, kWindow, false, gfx::PointF(x + 0.5f, y + 0.5f));
      ExpectColorNear(bitmap.getColor(x, y), SkColorGetR(model),
                      SkColorGetG(model), SkColorGetB(model), 3);
    }
  }
}

TEST_F(AerieFramePaintTest, TheMirroredPaintLightsTheRightCorner) {
  const FramePalette p =
      ResolveFramePalette(ThemeId::kVivid, ColorMode::kLight, "dusk");
  gfx::Canvas left(kWindow, 1.0f, true);
  gfx::Canvas right(kWindow, 1.0f, true);
  PaintFrame(&left, p, kWindow, false, gfx::Rect(300, 40, 1000, 840));
  PaintFrame(&right, p, kWindow, true, gfx::Rect(300, 40, 1000, 840));
  const SkColor a = left.GetBitmap().getColor(2, 2);
  ExpectColorNear(right.GetBitmap().getColor(1437, 2), SkColorGetR(a),
                  SkColorGetG(a), SkColorGetB(a), 1);
}

TEST_F(AerieFramePaintTest, TheCardShadowDarkensTheFrameJustOutsideTheCard) {
  const FramePalette p =
      ResolveFramePalette(ThemeId::kVivid, ColorMode::kLight, "dusk");
  const gfx::Rect card(300, 100, 1000, 700);
  gfx::Canvas with_card(kWindow, 1.0f, true);
  PaintFrame(&with_card, p, kWindow, false, card);
  gfx::Canvas no_card(kWindow, 1.0f, true);
  PaintFrame(&no_card, p, kWindow, false, gfx::Rect());
  const SkBitmap a = with_card.GetBitmap();
  const SkBitmap b = no_card.GetBitmap();
  // One pixel below the bottom edge: the shadow's offset puts it there.
  const int x = card.CenterPoint().x();
  const int y = card.bottom() + 1;
  EXPECT_LT(SkColorGetR(a.getColor(x, y)), SkColorGetR(b.getColor(x, y)));
  // Far from the card nothing changes.
  EXPECT_EQ(a.getColor(20, 20), b.getColor(20, 20));
}

// --- The repaint gate ----------------------------------------------------

TEST(AerieFrameTintStateTest, ThePaintingIsOnceAndThenOnStateChangeOnly) {
  FrameTintState state;
  FrameTintInputs inputs;
  inputs.colour = "dusk";
  inputs.size = kWindow;

  // The first state needs a paint; the same state again does not.
  EXPECT_TRUE(state.Update(inputs));
  state.RecordPaint();
  for (int frame = 0; frame < 60; ++frame) {
    EXPECT_FALSE(state.Update(inputs));
  }
  EXPECT_EQ(state.paint_count(), 1);

  // A Workspace colour change paints once.
  inputs.colour = "tide";
  EXPECT_TRUE(state.Update(inputs));
  state.RecordPaint();
  EXPECT_FALSE(state.Update(inputs));
  EXPECT_EQ(state.paint_count(), 2);

  // A mode change paints once.
  inputs.mode = ColorMode::kDark;
  EXPECT_TRUE(state.Update(inputs));
  state.RecordPaint();
  EXPECT_FALSE(state.Update(inputs));
  EXPECT_EQ(state.paint_count(), 3);

  // So do a resize, a card change and the side.
  inputs.size = gfx::Size(1000, 700);
  EXPECT_TRUE(state.Update(inputs));
  inputs.card = gfx::Rect(268, 8, 700, 680);
  EXPECT_TRUE(state.Update(inputs));
  inputs.sidebar_on_right = true;
  EXPECT_TRUE(state.Update(inputs));
  EXPECT_FALSE(state.Update(inputs));
}

// --- When the tint is on -------------------------------------------------

class AerieFrameEnabledTest : public testing::Test {
 public:
  AerieFrameEnabledTest() { aerie_sidebar::RegisterProfilePrefs(prefs_.registry()); }

 protected:
  TestingPrefServiceSimple prefs_;
};

TEST_F(AerieFrameEnabledTest, OnForANormalWindowWithTheSidebarLayout) {
  EXPECT_TRUE(IsFrameTintActive(prefs_, /*is_normal_window=*/true));
  EXPECT_FALSE(IsFrameTintActive(prefs_, /*is_normal_window=*/false));
}

TEST_F(AerieFrameEnabledTest, OffForTheHorizontalLayout) {
  prefs_.SetString(aerie_sidebar::kLayoutPref, aerie_sidebar::kLayoutHorizontal);
  EXPECT_FALSE(IsFrameTintActive(prefs_, true));
}

TEST(AerieFrameCardTest, TheCardRadiusIsRadiusXl) {
  EXPECT_EQ(CardRadius(), aerie::tokens::kRadiusXl);
}

// --- Radius and rounded frame gate, shade and glow colours (issue #178) -------

TEST(AerieFrameGateTest, TheFrameRadiusIsRadiusXlOnlyWhileTheTintIsOn) {
  EXPECT_EQ(FrameRadius(true, 8), aerie::tokens::kRadiusXl);
  EXPECT_EQ(FrameRadius(true, 3), aerie::tokens::kRadiusXl);
  // Helium's own radius, whatever it is, everywhere else.
  EXPECT_EQ(FrameRadius(false, 8), 8);
  EXPECT_EQ(FrameRadius(false, 0), 0);
  EXPECT_EQ(FrameRadius(false, 21), 21);
}

TEST(AerieFrameGateTest, TheRoundedFrameIsForcedOnOnlyWhileTheTintIsOn) {
  EXPECT_TRUE(RoundedFrameEnabled(true, false));
  EXPECT_TRUE(RoundedFrameEnabled(true, true));
  EXPECT_TRUE(RoundedFrameEnabled(false, true));
  EXPECT_FALSE(RoundedFrameEnabled(false, false));
}

TEST(AerieFramePaletteTest, TheShadeAndGlowColoursAreTheTokens) {
  for (ColorMode mode : {ColorMode::kLight, ColorMode::kDark}) {
    const FramePalette p = ResolveFramePalette(ThemeId::kVivid, mode, "dusk");
    EXPECT_EQ(p.shade_colour, aerie::tokens::kColorFrameShade);
    EXPECT_EQ(p.glow_colour, aerie::tokens::kColorFrameGlow);
  }
}

}  // namespace
}  // namespace aerie_frame
