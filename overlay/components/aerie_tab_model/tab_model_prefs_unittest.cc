// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_tab_model/tab_model_prefs.h"

#include "base/time/time.h"
#include "components/prefs/testing_pref_service.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace aerie_tab_model {

TEST(TabModelPrefsTest, DefaultsAreTwelveHoursAndOneHour) {
  TestingPrefServiceSimple prefs;
  RegisterProfilePrefs(prefs.registry());
  EXPECT_EQ(GetArchiveAfter(prefs), base::Hours(12));
  EXPECT_EQ(GetSleepAfter(prefs), base::Minutes(60));
}

TEST(TabModelPrefsTest, ArchiveAfterHasFourValues) {
  EXPECT_EQ(ParseArchiveAfter("12h"), base::Hours(12));
  EXPECT_EQ(ParseArchiveAfter("24h"), base::Hours(24));
  EXPECT_EQ(ParseArchiveAfter("7d"), base::Days(7));
  EXPECT_EQ(ParseArchiveAfter("never"), std::nullopt);
}

TEST(TabModelPrefsTest, AnUnknownArchiveValueFallsBackToTwelveHours) {
  EXPECT_EQ(ParseArchiveAfter(""), base::Hours(12));
  EXPECT_EQ(ParseArchiveAfter("3d"), base::Hours(12));
}

TEST(TabModelPrefsTest, ArchiveKeepIsThirtyDaysByDefaultAndHasFourValues) {
  TestingPrefServiceSimple prefs;
  RegisterProfilePrefs(prefs.registry());
  EXPECT_STREQ("aerie.tabs.archive_keep", kArchiveKeep);
  EXPECT_EQ(GetArchiveKeep(prefs), base::Days(30));
  EXPECT_EQ(ParseArchiveKeep("7d"), base::Days(7));
  EXPECT_EQ(ParseArchiveKeep("30d"), base::Days(30));
  EXPECT_EQ(ParseArchiveKeep("90d"), base::Days(90));
  EXPECT_EQ(ParseArchiveKeep("forever"), std::nullopt);
  EXPECT_EQ(ParseArchiveKeep("3d"), base::Days(30));
  prefs.SetString(kArchiveKeep, "forever");
  EXPECT_EQ(GetArchiveKeep(prefs), std::nullopt);
}

TEST(TabModelPrefsTest, SleepAfterRunsFromFifteenMinutesToEightHoursOrNever) {
  EXPECT_EQ(SleepAfterFromMinutes(0), std::nullopt);
  EXPECT_EQ(SleepAfterFromMinutes(15), base::Minutes(15));
  EXPECT_EQ(SleepAfterFromMinutes(480), base::Hours(8));
  EXPECT_EQ(SleepAfterFromMinutes(5), base::Minutes(15));
  EXPECT_EQ(SleepAfterFromMinutes(10'000), base::Hours(8));
  EXPECT_EQ(SleepAfterFromMinutes(-3), base::Minutes(60));
}

TEST(TabModelPrefsTest, TheModelReadsWhatThePrefsHold) {
  TestingPrefServiceSimple prefs;
  RegisterProfilePrefs(prefs.registry());
  prefs.SetString(kArchiveAfter, "7d");
  prefs.SetInteger(kSleepAfterMinutes, 0);
  EXPECT_EQ(GetArchiveAfter(prefs), base::Days(7));
  EXPECT_EQ(GetSleepAfter(prefs), std::nullopt);
}

TEST(TabModelPrefsTest, FolderRulesAreOnByDefaultAndSwitchOffEverywhere) {
  TestingPrefServiceSimple prefs;
  RegisterProfilePrefs(prefs.registry());
  EXPECT_STREQ("aerie.folders.rules_enabled", kFolderRulesEnabled);
  EXPECT_TRUE(AreFolderRulesEnabled(prefs));
  prefs.SetBoolean(kFolderRulesEnabled, false);
  EXPECT_FALSE(AreFolderRulesEnabled(prefs));
}

}  // namespace aerie_tab_model
