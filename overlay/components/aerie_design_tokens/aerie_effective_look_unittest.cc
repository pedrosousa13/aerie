// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

// The effective look (issue #185): which Theme and colour mode the chrome is
// drawn in, from the global choice, a Workspace override and what the operating
// system asks for. The table is docs/sidebar-design.md, "Themes" and
// "prefers-contrast".

#include "components/aerie_design_tokens/aerie_appearance_prefs.h"
#include "components/aerie_design_tokens/aerie_effective_look.h"
#include "components/aerie_design_tokens/aerie_tokens.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/testing_pref_service.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace aerie {
namespace {

using tokens::ColorMode;
using tokens::EffectiveLook;
using tokens::EffectiveLookInputs;
using tokens::ModeChoice;
using tokens::ResolveEffectiveLook;
using tokens::ThemeId;

TEST(AerieEffectiveLookTest, NothingChosenIsVividInTheSystemMode) {
  EffectiveLookInputs in;
  EffectiveLook look = ResolveEffectiveLook(in);
  EXPECT_EQ(look.theme, ThemeId::kVivid);
  EXPECT_EQ(look.mode, ColorMode::kLight);
  EXPECT_FALSE(look.system_colours);

  in.os_dark = true;
  look = ResolveEffectiveLook(in);
  EXPECT_EQ(look.theme, ThemeId::kVivid);
  EXPECT_EQ(look.mode, ColorMode::kDark);
}

TEST(AerieEffectiveLookTest, AnExplicitModeBeatsTheOperatingSystem) {
  EffectiveLookInputs in;
  in.os_dark = true;
  in.global_mode = ModeChoice::kLight;
  EXPECT_EQ(ResolveEffectiveLook(in).mode, ColorMode::kLight);
  in.os_dark = false;
  in.global_mode = ModeChoice::kDark;
  EXPECT_EQ(ResolveEffectiveLook(in).mode, ColorMode::kDark);
}

TEST(AerieEffectiveLookTest, OledInLightModeShowsClean) {
  EffectiveLookInputs in;
  in.global_theme = ThemeId::kOled;
  in.global_mode = ModeChoice::kLight;
  EffectiveLook look = ResolveEffectiveLook(in);
  EXPECT_EQ(look.theme, ThemeId::kClean);
  EXPECT_EQ(look.mode, ColorMode::kLight);

  in.global_mode = ModeChoice::kDark;
  look = ResolveEffectiveLook(in);
  EXPECT_EQ(look.theme, ThemeId::kOled);
  EXPECT_EQ(look.mode, ColorMode::kDark);

  // System mode follows the operating system into and out of Clean.
  in.global_mode = ModeChoice::kSystem;
  in.os_dark = false;
  EXPECT_EQ(ResolveEffectiveLook(in).theme, ThemeId::kClean);
  in.os_dark = true;
  EXPECT_EQ(ResolveEffectiveLook(in).theme, ThemeId::kOled);
}

TEST(AerieEffectiveLookTest, PrefersContrastMoreSelectsHighContrast) {
  EffectiveLookInputs in;
  in.os_contrast_more = true;
  EffectiveLook look = ResolveEffectiveLook(in);
  EXPECT_EQ(look.theme, ThemeId::kHighContrast);
  EXPECT_EQ(look.mode, ColorMode::kLight);
  in.os_dark = true;
  look = ResolveEffectiveLook(in);
  EXPECT_EQ(look.theme, ThemeId::kHighContrast);
  EXPECT_EQ(look.mode, ColorMode::kDark);
}

TEST(AerieEffectiveLookTest, AThemeChosenExplicitlyBeatsTheContrastSwitch) {
  EffectiveLookInputs in;
  in.os_contrast_more = true;
  in.global_theme = ThemeId::kSoft;
  EXPECT_EQ(ResolveEffectiveLook(in).theme, ThemeId::kSoft);
  // Choosing the default theme is a choice too.
  in.global_theme = ThemeId::kVivid;
  EXPECT_EQ(ResolveEffectiveLook(in).theme, ThemeId::kVivid);
}

TEST(AerieEffectiveLookTest, AWorkspaceOverrideBeatsTheGlobalChoice) {
  EffectiveLookInputs in;
  in.global_theme = ThemeId::kSoft;
  in.global_mode = ModeChoice::kLight;
  in.workspace_theme = ThemeId::kMono;
  in.workspace_mode = ModeChoice::kDark;
  EffectiveLook look = ResolveEffectiveLook(in);
  EXPECT_EQ(look.theme, ThemeId::kMono);
  EXPECT_EQ(look.mode, ColorMode::kDark);

  // "default" (no override) uses the global choice.
  in.workspace_theme.reset();
  in.workspace_mode.reset();
  look = ResolveEffectiveLook(in);
  EXPECT_EQ(look.theme, ThemeId::kSoft);
  EXPECT_EQ(look.mode, ColorMode::kLight);
}

TEST(AerieEffectiveLookTest, AWorkspaceOverrideBeatsTheContrastSwitchToo) {
  EffectiveLookInputs in;
  in.os_contrast_more = true;
  in.workspace_theme = ThemeId::kClean;
  EXPECT_EQ(ResolveEffectiveLook(in).theme, ThemeId::kClean);
}

TEST(AerieEffectiveLookTest, AWorkspaceSystemModeFollowsTheOperatingSystem) {
  EffectiveLookInputs in;
  in.global_mode = ModeChoice::kLight;
  in.workspace_mode = ModeChoice::kSystem;
  in.os_dark = true;
  EXPECT_EQ(ResolveEffectiveLook(in).mode, ColorMode::kDark);
}

TEST(AerieEffectiveLookTest, ForcedColoursSwitchToSystemColoursAndKeepTheRest) {
  EffectiveLookInputs in;
  in.forced_colors = true;
  in.global_theme = ThemeId::kSoft;
  in.os_dark = true;
  const EffectiveLook look = ResolveEffectiveLook(in);
  EXPECT_TRUE(look.system_colours);
  EXPECT_EQ(look.theme, ThemeId::kSoft);
  EXPECT_EQ(look.mode, ColorMode::kDark);
}

TEST(AerieEffectiveLookTest, EveryResultHasATableInItsMode) {
  for (int t = 0; t < tokens::kThemeCount; ++t) {
    for (ModeChoice mode :
         {ModeChoice::kSystem, ModeChoice::kLight, ModeChoice::kDark}) {
      for (bool os_dark : {false, true}) {
        EffectiveLookInputs in;
        in.global_theme = static_cast<ThemeId>(t);
        in.global_mode = mode;
        in.os_dark = os_dark;
        const EffectiveLook look = ResolveEffectiveLook(in);
        EXPECT_TRUE(tokens::ThemeSupportsMode(look.theme, look.mode))
            << t << " " << static_cast<int>(mode) << " " << os_dark;
      }
    }
  }
}

// ---- the prefs ----------------------------------------------------------

TEST(AerieAppearancePrefsTest, TheNamesAndDefaultsAreFixed) {
  EXPECT_STREQ(appearance::kThemePref, "aerie.appearance.theme");
  EXPECT_STREQ(appearance::kModePref, "aerie.appearance.mode");
  TestingPrefServiceSimple prefs;
  appearance::RegisterProfilePrefs(prefs.registry());
  EXPECT_EQ(prefs.GetString(appearance::kThemePref), "vivid");
  EXPECT_EQ(prefs.GetString(appearance::kModePref), "system");
}

TEST(AerieAppearancePrefsTest, ThemeIdsAndModesParse) {
  for (int t = 0; t < tokens::kThemeCount; ++t) {
    EXPECT_EQ(appearance::ParseThemeId(tokens::kThemeIds[t]),
              static_cast<ThemeId>(t));
  }
  EXPECT_FALSE(appearance::ParseThemeId("default").has_value());
  EXPECT_FALSE(appearance::ParseThemeId("neon").has_value());
  EXPECT_FALSE(appearance::ParseThemeId("").has_value());
  EXPECT_EQ(appearance::ParseModeChoice("system"), ModeChoice::kSystem);
  EXPECT_EQ(appearance::ParseModeChoice("light"), ModeChoice::kLight);
  EXPECT_EQ(appearance::ParseModeChoice("dark"), ModeChoice::kDark);
  EXPECT_FALSE(appearance::ParseModeChoice("default").has_value());
  EXPECT_FALSE(appearance::ParseModeChoice("sepia").has_value());
}

class AerieAppearanceInputsTest : public testing::Test {
 public:
  AerieAppearanceInputsTest() {
    appearance::RegisterProfilePrefs(prefs_.registry());
  }

 protected:
  TestingPrefServiceSimple prefs_;
};

TEST_F(AerieAppearanceInputsTest, ANeverChosenThemeIsNotAChoice) {
  const EffectiveLookInputs in = appearance::ReadGlobalChoice(prefs_);
  EXPECT_FALSE(in.global_theme.has_value());
  EXPECT_EQ(in.global_mode, ModeChoice::kSystem);
}

TEST_F(AerieAppearanceInputsTest, ChoosingVividIsAChoice) {
  prefs_.SetString(appearance::kThemePref, "vivid");
  EXPECT_EQ(appearance::ReadGlobalChoice(prefs_).global_theme, ThemeId::kVivid);
}

TEST_F(AerieAppearanceInputsTest, TheChosenThemeAndModeAreRead) {
  prefs_.SetString(appearance::kThemePref, "oled");
  prefs_.SetString(appearance::kModePref, "dark");
  const EffectiveLookInputs in = appearance::ReadGlobalChoice(prefs_);
  EXPECT_EQ(in.global_theme, ThemeId::kOled);
  EXPECT_EQ(in.global_mode, ModeChoice::kDark);
}

TEST_F(AerieAppearanceInputsTest, ABadStoredValueReadsAsNotChosenAndSystem) {
  prefs_.SetString(appearance::kThemePref, "neon");
  prefs_.SetString(appearance::kModePref, "sepia");
  const EffectiveLookInputs in = appearance::ReadGlobalChoice(prefs_);
  EXPECT_FALSE(in.global_theme.has_value());
  EXPECT_EQ(in.global_mode, ModeChoice::kSystem);
}

}  // namespace
}  // namespace aerie
