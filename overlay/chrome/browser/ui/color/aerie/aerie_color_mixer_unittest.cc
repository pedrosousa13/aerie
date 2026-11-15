// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

// The Aerie ColorMixer (issue #185): Chromium's own surfaces take the
// effective theme's colours, in both modes, and nothing changes before an Aerie
// window sets a look or while forced colours are on.

#include "chrome/browser/ui/color/aerie/aerie_color_mixer.h"

#include <cstdio>
#include <memory>

#include "base/time/time.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "components/aerie_frame/aerie_frame_look.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/color/color_id.h"
#include "ui/color/color_mixer.h"
#include "ui/color/color_recipe.h"
#include "ui/color/color_mixers.h"
#include "ui/color/color_provider.h"
#include "ui/color/color_provider_key.h"
#include "ui/gfx/color_palette.h"

namespace aerie_color {
namespace {

using aerie::tokens::ColorMode;
using aerie::tokens::ThemeId;

class AerieColorMixerTest : public testing::Test {
 public:
  AerieColorMixerTest() { ResetActiveLookForTesting(); }
  ~AerieColorMixerTest() override { ResetActiveLookForTesting(); }

 protected:
  static ui::ColorProviderKey Key(ui::ColorProviderKey::ColorMode mode) {
    ui::ColorProviderKey key;
    key.color_mode = mode;
    return key;
  }

  // A provider the way Chromium builds one, then the Aerie mixer on top.
  static std::unique_ptr<ui::ColorProvider> Build(
      const ui::ColorProviderKey& key) {
    auto provider = std::make_unique<ui::ColorProvider>();
    ui::AddColorMixers(provider.get(), key);
    AddAerieColorMixer(provider.get(), key);
    return provider;
  }
};

TEST_F(AerieColorMixerTest, NothingChangesBeforeALookIsSet) {
  const ui::ColorProviderKey key = Key(ui::ColorProviderKey::ColorMode::kLight);
  ui::ColorProvider before;
  ui::AddColorMixers(&before, key);
  const auto after_ptr = Build(key);
  const ui::ColorProvider& after = *after_ptr;
  for (ui::ColorId id : {ui::kColorMenuBackground, ui::kColorSysSurface,
                         ui::kColorPrimaryForeground, ui::kColorAccent}) {
    EXPECT_EQ(before.GetColor(id), after.GetColor(id));
  }
}

TEST_F(AerieColorMixerTest, SettingTheLookSaysWhetherItChanged) {
  EXPECT_TRUE(SetActiveLook(ThemeId::kVivid, "dusk"));
  EXPECT_FALSE(SetActiveLook(ThemeId::kVivid, "dusk"));
  EXPECT_TRUE(SetActiveLook(ThemeId::kSoft, "dusk"));
  EXPECT_TRUE(SetActiveLook(ThemeId::kSoft, "tide"));
  EXPECT_EQ(GetActiveLook().theme, ThemeId::kSoft);
  EXPECT_EQ(GetActiveLook().colour, "tide");
}

TEST_F(AerieColorMixerTest, EveryRoleTakesTheThemeColourInBothModes) {
  for (ThemeId theme : {ThemeId::kVivid, ThemeId::kOled, ThemeId::kHighContrast,
                        ThemeId::kMono}) {
    SetActiveLook(theme, "tide");
    for (auto key_mode : {ui::ColorProviderKey::ColorMode::kLight,
                          ui::ColorProviderKey::ColorMode::kDark}) {
      const ColorMode mode = key_mode == ui::ColorProviderKey::ColorMode::kDark
                                 ? ColorMode::kDark
                                 : ColorMode::kLight;
      const aerie_frame::ChromeColors c =
          aerie_frame::ResolveChromeColors(theme, mode, "tide");
      const auto p_ptr = Build(Key(key_mode));
      const ui::ColorProvider& p = *p_ptr;
      EXPECT_EQ(p.GetColor(ui::kColorMenuBackground), c.surface);
      EXPECT_EQ(p.GetColor(ui::kColorBubbleBackground), c.surface);
      EXPECT_EQ(p.GetColor(ui::kColorDialogBackground), c.surface);
      EXPECT_EQ(p.GetColor(kColorOmniboxResultsBackground), c.surface);
      EXPECT_EQ(p.GetColor(ui::kColorSysSurface), c.surface);
      EXPECT_EQ(p.GetColor(ui::kColorMenuItemForeground), c.text);
      EXPECT_EQ(p.GetColor(ui::kColorDialogForeground), c.text);
      EXPECT_EQ(p.GetColor(ui::kColorSysOnSurface), c.text);
      EXPECT_EQ(p.GetColor(ui::kColorSecondaryForeground), c.text_muted);
      EXPECT_EQ(p.GetColor(ui::kColorSysOnSurfaceSubtle), c.text_muted);
      EXPECT_EQ(p.GetColor(ui::kColorAccent), c.accent);
      EXPECT_EQ(p.GetColor(ui::kColorSysPrimary), c.accent);
      EXPECT_EQ(p.GetColor(ui::kColorFocusableBorderFocused), c.focus_ring);
      EXPECT_EQ(p.GetColor(ui::kColorMenuSeparator), c.divider);
      EXPECT_EQ(p.GetColor(ui::kColorSysDivider), c.divider);
      EXPECT_EQ(p.GetColor(ui::kColorMenuBorder), c.hairline);
    }
  }
}

TEST_F(AerieColorMixerTest, TheOtherModeHasOtherColours) {
  SetActiveLook(ThemeId::kVivid, "dusk");
  const auto light_ptr = Build(Key(ui::ColorProviderKey::ColorMode::kLight));
  const ui::ColorProvider& light = *light_ptr;
  const auto dark_ptr = Build(Key(ui::ColorProviderKey::ColorMode::kDark));
  const ui::ColorProvider& dark = *dark_ptr;
  EXPECT_NE(light.GetColor(ui::kColorMenuBackground),
            dark.GetColor(ui::kColorMenuBackground));
  EXPECT_NE(light.GetColor(ui::kColorPrimaryForeground),
            dark.GetColor(ui::kColorPrimaryForeground));
}

TEST_F(AerieColorMixerTest, AChangedThemeChangesTheNextProvider) {
  SetActiveLook(ThemeId::kVivid, "dusk");
  const SkColor vivid =
      Build(Key(ui::ColorProviderKey::ColorMode::kDark))->GetColor(ui::kColorMenuBackground);
  SetActiveLook(ThemeId::kOled, "dusk");
  const SkColor oled =
      Build(Key(ui::ColorProviderKey::ColorMode::kDark))->GetColor(ui::kColorMenuBackground);
  EXPECT_NE(vivid, oled);
  EXPECT_EQ(oled, SkColorSetRGB(0x0E, 0x0E, 0x10));
}

TEST_F(AerieColorMixerTest, OledInLightModeShowsClean) {
  SetActiveLook(ThemeId::kOled, "dusk");
  const SkColor menu =
      Build(Key(ui::ColorProviderKey::ColorMode::kLight))->GetColor(ui::kColorMenuBackground);
  EXPECT_EQ(menu, aerie_frame::ResolveChromeColors(ThemeId::kClean,
                                                   ColorMode::kLight, "dusk")
                      .surface);
}

TEST_F(AerieColorMixerTest, ForcedColoursKeepTheSystemColours) {
  SetActiveLook(ThemeId::kVivid, "dusk");
  ui::ColorProviderKey key = Key(ui::ColorProviderKey::ColorMode::kLight);
  key.forced_colors = ui::ColorProviderKey::ForcedColors::kSystem;
  ui::ColorProvider before;
  ui::AddColorMixers(&before, key);
  const auto after_ptr = Build(key);
  const ui::ColorProvider& after = *after_ptr;
  EXPECT_EQ(before.GetColor(ui::kColorMenuBackground),
            after.GetColor(ui::kColorMenuBackground));
  EXPECT_EQ(before.GetColor(ui::kColorPrimaryForeground),
            after.GetColor(ui::kColorPrimaryForeground));
}

TEST_F(AerieColorMixerTest, ItWinsOverAnEarlierMixer) {
  SetActiveLook(ThemeId::kVivid, "dusk");
  const ui::ColorProviderKey key = Key(ui::ColorProviderKey::ColorMode::kLight);
  ui::ColorProvider provider;
  provider.AddMixer()[ui::kColorMenuBackground] = {SK_ColorRED};
  AddAerieColorMixer(&provider, key);
  EXPECT_EQ(provider.GetColor(ui::kColorMenuBackground),
            aerie_frame::ResolveChromeColors(ThemeId::kVivid, ColorMode::kLight, "dusk")
                .surface);
}

// The issue asks that the mixer add no measurable cost to a ColorProvider
// build. This builds 1,000 providers the way Chromium does (the shared ui
// mixers) without and with the Aerie mixer, and reports both.
TEST_F(AerieColorMixerTest, TheMixerAddsNoMeasurableCostToAProviderBuild) {
  SetActiveLook(ThemeId::kVivid, "dusk");
  const ui::ColorProviderKey key = Key(ui::ColorProviderKey::ColorMode::kDark);
  constexpr int kBuilds = 1000;
  // Reading a colour forces the lazy mixers to run, so a build that is never
  // read does not hide a cost.
  auto build_without = [&] {
    ui::ColorProvider p;
    ui::AddColorMixers(&p, key);
    return p.GetColor(ui::kColorMenuBackground);
  };
  auto build_with = [&] {
    ui::ColorProvider p;
    ui::AddColorMixers(&p, key);
    AddAerieColorMixer(&p, key);
    return p.GetColor(ui::kColorMenuBackground);
  };
  // Warm up, then take the best of three rounds for each, to keep noise out.
  build_without();
  build_with();
  auto time = [&](auto&& build) {
    base::TimeDelta best = base::TimeDelta::Max();
    for (int round = 0; round < 3; ++round) {
      const base::TimeTicks start = base::TimeTicks::Now();
      for (int i = 0; i < kBuilds; ++i) {
        build();
      }
      best = std::min(best, base::TimeTicks::Now() - start);
    }
    return best;
  };
  const base::TimeDelta without = time(build_without);
  const base::TimeDelta with = time(build_with);
  const double per_build_us = (with - without).InMicrosecondsF() / kBuilds;
  std::printf(
      "AERIE_COLOR_MIXER_COST without=%.1f us/build with=%.1f us/build "
      "delta=%.2f us/build (%d builds)\n",
      without.InMicrosecondsF() / kBuilds, with.InMicrosecondsF() / kBuilds,
      per_build_us, kBuilds);
  // A provider build is tens of microseconds; the mixer is a few dozen
  // constant recipes. 25 us is far above its cost and far below a visible one.
  EXPECT_LT(per_build_us, 25.0);
}

// Text on the accent is readable in every theme and mode: 4.5:1, and 7:1 under
// High contrast. Chromium derives these roles from its own primary otherwise.
TEST_F(AerieColorMixerTest, TextOnTheAccentIsReadableInEveryThemeAndMode) {
  for (int i = 0; i < aerie::tokens::kThemeCount; ++i) {
    const ThemeId theme = static_cast<ThemeId>(i);
    for (ColorMode mode : {ColorMode::kLight, ColorMode::kDark}) {
      if (!aerie::tokens::ThemeSupportsMode(theme, mode)) {
        continue;
      }
      for (const char* colour : {"dusk", "tide", "moss", "ember", "#336699"}) {
        SCOPED_TRACE(std::string(aerie::tokens::kThemeIds[i]) + "/" + colour +
                     (mode == ColorMode::kDark ? "/dark" : "/light"));
        SetActiveLook(theme, colour);
        const auto provider = Build(Key(mode == ColorMode::kDark
                                            ? ui::ColorProviderKey::ColorMode::kDark
                                            : ui::ColorProviderKey::ColorMode::kLight));
        const double floor = theme == ThemeId::kHighContrast ? 7.0 : 4.5;
        for (ui::ColorId on : {ui::kColorSysOnPrimary,
                               ui::kColorButtonForegroundProminent}) {
          EXPECT_GE(aerie_frame::ContrastRatio(
                        provider->GetColor(on),
                        provider->GetColor(ui::kColorSysPrimary)),
                    floor);
        }
        EXPECT_GE(aerie_frame::ContrastRatio(
                      provider->GetColor(ui::kColorSysOnPrimaryContainer),
                      provider->GetColor(ui::kColorSysPrimaryContainer)),
                  floor);
      }
    }
  }
}

}  // namespace
}  // namespace aerie_color
