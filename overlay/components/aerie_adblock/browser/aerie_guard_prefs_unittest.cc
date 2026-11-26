// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_adblock/browser/aerie_guard_prefs.h"

#include "components/prefs/testing_pref_service.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace aerie_guard {

TEST(AerieGuardPrefsTest, TheCountIsShownByDefault) {
  TestingPrefServiceSimple prefs;
  RegisterProfilePrefs(prefs.registry());
  EXPECT_TRUE(prefs.GetBoolean(kShowBlockedCount));
  EXPECT_TRUE(prefs.FindPreference(kShowBlockedCount)->IsDefaultValue());
}

TEST(AerieGuardPrefsTest, TheUserCanHideTheCountAndShowItAgain) {
  TestingPrefServiceSimple prefs;
  RegisterProfilePrefs(prefs.registry());
  prefs.SetBoolean(kShowBlockedCount, false);
  EXPECT_FALSE(prefs.GetBoolean(kShowBlockedCount));
  prefs.SetBoolean(kShowBlockedCount, true);
  EXPECT_TRUE(prefs.GetBoolean(kShowBlockedCount));
}

TEST(AerieGuardPrefsTest, TheLanguageCardIsShownUntilTheUserDismissesIt) {
  TestingPrefServiceSimple prefs;
  RegisterProfilePrefs(prefs.registry());
  EXPECT_FALSE(prefs.GetBoolean(kLanguageCardDismissed));
  EXPECT_TRUE(prefs.FindPreference(kLanguageCardDismissed)->IsDefaultValue());
  prefs.SetBoolean(kLanguageCardDismissed, true);
  EXPECT_TRUE(prefs.GetBoolean(kLanguageCardDismissed));
}

}  // namespace aerie_guard
