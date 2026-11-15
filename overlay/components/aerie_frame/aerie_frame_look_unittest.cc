// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

// The parts of a theme that issue #185 adds to the frame: the colours Chromium's
// own surfaces get through the ColorMixer, the system-colour frame of forced
// colours, the strip on the sidebar's top edge, and the card's hairline.

#include <set>
#include <string>

#include "base/memory/discardable_memory_allocator.h"
#include "base/test/test_discardable_memory_allocator.h"
#include "components/aerie_design_tokens/aerie_tokens.h"
#include "components/aerie_frame/aerie_frame_look.h"
#include "components/aerie_frame/aerie_frame_paint.h"
#include "components/aerie_frame/aerie_frame_palette.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/gfx/canvas.h"

namespace aerie_frame {
namespace {

using aerie::tokens::ColorMode;
using aerie::tokens::ThemeId;

constexpr gfx::Size kWindow(1440, 900);

// ---- Chromium's surfaces ----------------------------------------------------

TEST(AerieChromeColorsTest, TheColoursAreTheThemeTokens) {
  for (int t = 0; t < aerie::tokens::kThemeCount; ++t) {
    for (ColorMode mode : {ColorMode::kLight, ColorMode::kDark}) {
      const ThemeId theme = static_cast<ThemeId>(t);
      const auto& tokens = aerie::tokens::GetThemeTokens(theme, mode);
      const ChromeColors c = ResolveChromeColors(theme, mode, "dusk");
      EXPECT_EQ(c.surface, tokens.color_surface_popover) << t;
      EXPECT_EQ(c.text, tokens.color_text) << t;
      EXPECT_EQ(c.text_muted, tokens.color_text_muted) << t;
      EXPECT_EQ(c.divider, tokens.color_divider) << t;
      EXPECT_EQ(c.hairline, tokens.color_border_hairline) << t;
    }
  }
}

TEST(AerieChromeColorsTest, TheAccentAndTheFocusRingAreTheFramesAccent) {
  for (ColorMode mode : {ColorMode::kLight, ColorMode::kDark}) {
    const ChromeColors c = ResolveChromeColors(ThemeId::kVivid, mode, "tide");
    EXPECT_EQ(c.accent, ResolveFramePalette(ThemeId::kVivid, mode, "tide").accent);
    EXPECT_EQ(c.focus_ring, c.accent);
  }
  // The Workspace colour moves the accent and nothing else.
  const ChromeColors dusk =
      ResolveChromeColors(ThemeId::kVivid, ColorMode::kLight, "dusk");
  const ChromeColors tide =
      ResolveChromeColors(ThemeId::kVivid, ColorMode::kLight, "tide");
  EXPECT_NE(dusk.accent, tide.accent);
  EXPECT_EQ(dusk.surface, tide.surface);
  EXPECT_EQ(dusk.text, tide.text);
}

TEST(AerieChromeColorsTest, OledIsBlackAndHighContrastIsBlackOnWhite) {
  // Copied from tokens.json on purpose: a token edit must change the test.
  EXPECT_EQ(ResolveChromeColors(ThemeId::kOled, ColorMode::kDark, "dusk").surface,
            SkColorSetRGB(0x0E, 0x0E, 0x10));
  EXPECT_EQ(
      ResolveChromeColors(ThemeId::kHighContrast, ColorMode::kLight, "dusk").text,
      SK_ColorBLACK);
  EXPECT_EQ(
      ResolveChromeColors(ThemeId::kHighContrast, ColorMode::kLight, "dusk").surface,
      SK_ColorWHITE);
  EXPECT_EQ(
      ResolveChromeColors(ThemeId::kHighContrast, ColorMode::kDark, "dusk").surface,
      SK_ColorBLACK);
}

TEST(AerieChromeColorsTest, OledInLightModeIsClean) {
  const ChromeColors oled =
      ResolveChromeColors(ThemeId::kOled, ColorMode::kLight, "dusk");
  const ChromeColors clean =
      ResolveChromeColors(ThemeId::kClean, ColorMode::kLight, "dusk");
  EXPECT_EQ(oled.surface, clean.surface);
  EXPECT_EQ(oled.text, clean.text);
}

TEST(AerieChromeColorsTest, TextReadsOnTheSurfaceInEveryThemeAndMode) {
  for (int t = 0; t < aerie::tokens::kThemeCount; ++t) {
    const ThemeId theme = static_cast<ThemeId>(t);
    const double minimum = theme == ThemeId::kHighContrast ? 7.0 : 4.5;
    for (ColorMode mode : {ColorMode::kLight, ColorMode::kDark}) {
      const ChromeColors c = ResolveChromeColors(theme, mode, "dusk");
      EXPECT_GE(ContrastRatio(c.text, c.surface), minimum) << t;
      EXPECT_GE(ContrastRatio(c.text_muted, c.surface), minimum) << t;
    }
  }
}

// ---- Forced colours -----------------------------------------------------------

constexpr SystemColours kSystem{SK_ColorBLACK, SK_ColorWHITE,
                                SkColorSetRGB(0x1A, 0xEB, 0xFF)};

TEST(AerieSystemPaletteTest, TheFrameIsOneFlatSystemColour) {
  const FramePalette p = ResolveSystemFramePalette(kSystem);
  EXPECT_EQ(p.frame_base, SK_ColorBLACK);
  EXPECT_EQ(p.text, SK_ColorWHITE);
  EXPECT_EQ(p.text_muted, SK_ColorWHITE);
  EXPECT_EQ(p.accent, kSystem.accent);
  EXPECT_EQ(p.shade, 0.0f);
  EXPECT_EQ(p.glow, 0.0f);
  EXPECT_EQ(p.strip_height, 0);
  for (const gfx::PointF& point :
       {gfx::PointF(0, 0), gfx::PointF(700, 400), gfx::PointF(1439, 899)}) {
    EXPECT_EQ(FrameColorAt(p, kWindow, false, point), SK_ColorBLACK);
  }
}

TEST(AerieSystemPaletteTest, TheCardHasAHairlineInTheTextColour) {
  const FramePalette p = ResolveSystemFramePalette(kSystem);
  EXPECT_EQ(p.card_surface, SK_ColorBLACK);
  EXPECT_GE(p.card_border_width, 1);
  EXPECT_EQ(p.card_border_color, SK_ColorWHITE);
}

TEST(AerieSystemPaletteTest, TheTintOfAWorkspaceColourIsIgnored) {
  // Whatever the theme tables say, nothing of the Workspace colour is in it.
  const FramePalette p = ResolveSystemFramePalette(kSystem);
  EXPECT_EQ(p.stop_count, 1);
  EXPECT_EQ(p.stops[0], SK_ColorBLACK);
}

// ---- The inputs of the tint ------------------------------------------------

TEST(AerieTintInputsTest, SystemColoursReplaceTheThemePalette) {
  FrameTintInputs in;
  in.theme = ThemeId::kVivid;
  in.mode = ColorMode::kLight;
  in.colour = "dusk";
  EXPECT_EQ(PaletteOf(in).frame_base,
            ResolveFramePalette(ThemeId::kVivid, ColorMode::kLight, "dusk").frame_base);
  in.system_colours = kSystem;
  const FramePalette p = PaletteOf(in);
  EXPECT_EQ(p.frame_base, SK_ColorBLACK);
  EXPECT_EQ(p.stop_count, 1);
}

TEST(AerieTintInputsTest, AChangeOfTheSystemColoursOrTheThemeNeedsAPaint) {
  FrameTintState state;
  FrameTintInputs in;
  EXPECT_TRUE(state.Update(in));
  EXPECT_FALSE(state.Update(in));
  in.theme = ThemeId::kOled;
  EXPECT_TRUE(state.Update(in));
  EXPECT_FALSE(state.Update(in));
  in.system_colours = kSystem;
  EXPECT_TRUE(state.Update(in));
  EXPECT_FALSE(state.Update(in));
  in.system_colours = SystemColours{SK_ColorWHITE, SK_ColorBLACK, SK_ColorBLUE};
  EXPECT_TRUE(state.Update(in));
}

// ---- The strip -------------------------------------------------------------------

TEST(AerieFrameStripTest, CleanHasAThreePixelStripAndHighContrastFour) {
  EXPECT_EQ(ResolveFramePalette(ThemeId::kVivid, ColorMode::kLight, "dusk").strip_height, 0);
  EXPECT_EQ(ResolveFramePalette(ThemeId::kSoft, ColorMode::kDark, "dusk").strip_height, 0);
  EXPECT_EQ(ResolveFramePalette(ThemeId::kClean, ColorMode::kLight, "dusk").strip_height, 3);
  EXPECT_EQ(ResolveFramePalette(ThemeId::kHighContrast, ColorMode::kDark, "dusk").strip_height, 4);
  EXPECT_EQ(ResolveFramePalette(ThemeId::kOled, ColorMode::kDark, "dusk").strip_height, 0);
}

TEST(AerieFrameStripTest, TheStripIsTheWorkspaceGradientAtFullStrength) {
  const FramePalette p =
      ResolveFramePalette(ThemeId::kClean, ColorMode::kLight, "dusk");
  const auto& preset = aerie::tokens::kWorkspacePresets[0];
  ASSERT_EQ(p.strip_stop_count, preset.stop_count);
  for (int i = 0; i < preset.stop_count; ++i) {
    EXPECT_EQ(p.strip_stops[i], preset.stops[i]);
  }
}

class AerieFrameLookPaintTest : public testing::Test {
 public:
  AerieFrameLookPaintTest() {
    base::DiscardableMemoryAllocator::SetInstance(&allocator_);
  }
  ~AerieFrameLookPaintTest() override {
    base::DiscardableMemoryAllocator::SetInstance(nullptr);
  }

 private:
  base::TestDiscardableMemoryAllocator allocator_;
};

TEST_F(AerieFrameLookPaintTest, TheStripIsPaintedAcrossTheSidebarColumnOnly) {
  const FramePalette p =
      ResolveFramePalette(ThemeId::kClean, ColorMode::kLight, "dusk");
  const gfx::Rect card(268, 8, 1164, 884);
  gfx::Canvas canvas(kWindow, 1.0f, true);
  PaintFrame(&canvas, p, kWindow, false, card);
  const SkBitmap bitmap = canvas.GetBitmap();
  // The first stop at the left end, the last at the right end of the column.
  const SkColor first = bitmap.getColor(1, 1);
  EXPECT_NEAR(SkColorGetR(first), SkColorGetR(p.strip_stops[0]), 6);
  EXPECT_NEAR(SkColorGetG(first), SkColorGetG(p.strip_stops[0]), 6);
  const SkColor last = bitmap.getColor(card.x() - 2, 1);
  const SkColor want = p.strip_stops[p.strip_stop_count - 1];
  EXPECT_NEAR(SkColorGetR(last), SkColorGetR(want), 8);
  // Below the strip the frame is the tinted gradient, not the strip.
  EXPECT_NE(bitmap.getColor(1, p.strip_height + 2), first);
  // Past the column, at the same height, it is the frame.
  EXPECT_NE(bitmap.getColor(card.x() + 20, 1), last);
}

TEST_F(AerieFrameLookPaintTest, TheStripFollowsTheSidebarToTheRight) {
  const FramePalette p =
      ResolveFramePalette(ThemeId::kClean, ColorMode::kLight, "dusk");
  const gfx::Rect card(8, 8, 1164, 884);
  gfx::Canvas canvas(kWindow, 1.0f, true);
  PaintFrame(&canvas, p, kWindow, true, card);
  const SkBitmap bitmap = canvas.GetBitmap();
  // Mirrored: the first stop is at the window's right edge.
  const SkColor edge = bitmap.getColor(kWindow.width() - 2, 1);
  EXPECT_NEAR(SkColorGetR(edge), SkColorGetR(p.strip_stops[0]), 6);
}

TEST_F(AerieFrameLookPaintTest, NoStripMeansNoStripPixels) {
  const FramePalette p =
      ResolveFramePalette(ThemeId::kVivid, ColorMode::kLight, "dusk");
  gfx::Canvas with_strip_gap(kWindow, 1.0f, true);
  PaintFrame(&with_strip_gap, p, kWindow, false, gfx::Rect(268, 8, 1164, 884));
  const SkColor model = FrameColorAt(p, kWindow, false, gfx::PointF(1.5f, 1.5f));
  const SkColor got = with_strip_gap.GetBitmap().getColor(1, 1);
  EXPECT_NEAR(SkColorGetR(got), SkColorGetR(model), 3);
}

// ---- The card's hairline -------------------------------------------------------

TEST_F(AerieFrameLookPaintTest, HighContrastDrawsATwoPixelBorderOutsideTheCard) {
  const FramePalette p =
      ResolveFramePalette(ThemeId::kHighContrast, ColorMode::kLight, "dusk");
  ASSERT_EQ(p.card_border_width, 2);
  const gfx::Rect card(268, 100, 1000, 700);
  gfx::Canvas canvas(kWindow, 1.0f, true);
  PaintFrame(&canvas, p, kWindow, false, card);
  const SkBitmap bitmap = canvas.GetBitmap();
  const int y = card.CenterPoint().y();
  EXPECT_EQ(bitmap.getColor(card.x() - 1, y), p.card_border_color);
  EXPECT_EQ(bitmap.getColor(card.x() - 2, y), p.card_border_color);
  EXPECT_NE(bitmap.getColor(card.x() - 4, y), p.card_border_color);
}

TEST_F(AerieFrameLookPaintTest, VividDrawsNoBorder) {
  const FramePalette p =
      ResolveFramePalette(ThemeId::kVivid, ColorMode::kLight, "dusk");
  ASSERT_EQ(p.card_border_width, 0);
  const gfx::Rect card(268, 100, 1000, 700);
  gfx::Canvas with(kWindow, 1.0f, true);
  PaintFrame(&with, p, kWindow, false, card);
  // Two pixels left of the card is the shadow at most, never an opaque line.
  const SkColor a = with.GetBitmap().getColor(card.x() - 1, card.CenterPoint().y());
  const SkColor b = with.GetBitmap().getColor(card.x() - 6, card.CenterPoint().y());
  EXPECT_NEAR(SkColorGetR(a), SkColorGetR(b), 40);
}

// ---- The Settings swatches ----------------------------------------------

TEST(AerieThemeSwatchTest, EachThemeHasAGradientOfItsOwn) {
  std::set<std::string> seen;
  for (int t = 0; t < aerie::tokens::kThemeCount; ++t) {
    const std::string css = ThemeSwatchCss(static_cast<ThemeId>(t));
    EXPECT_EQ(css.rfind("linear-gradient(135deg, #", 0), 0u) << css;
    EXPECT_EQ(css.back(), ')') << css;
    EXPECT_TRUE(seen.insert(css).second) << "duplicate swatch " << css;
  }
}

TEST(AerieThemeSwatchTest, OledIsShownInDarkAndTheRestInLight) {
  EXPECT_NE(ThemeSwatchCss(ThemeId::kOled).find("#000000"), std::string::npos);
  EXPECT_NE(ThemeSwatchCss(ThemeId::kHighContrast).find("#FFFFFF"),
            std::string::npos);
}

}  // namespace
}  // namespace aerie_frame
