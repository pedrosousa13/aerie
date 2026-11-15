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

TEST(TabModelPrefsTest, TheTitleSettingsDefaultToTidyOnAndNoRules) {
  TestingPrefServiceSimple prefs;
  RegisterProfilePrefs(prefs.registry());
  EXPECT_STREQ("aerie.tabs.tidy_titles", kTidyTitles);
  EXPECT_STREQ("aerie.tabs.tidy_off_sites", kTidyOffSites);
  EXPECT_STREQ("aerie.tabs.rename_rules_enabled", kRenameRulesEnabled);
  EXPECT_STREQ("aerie.tabs.rename_rules", kRenameRules);
  const TitleSettings settings = GetTitleSettings(prefs);
  EXPECT_TRUE(settings.tidy_enabled);
  EXPECT_TRUE(settings.tidy_off_sites.empty());
  EXPECT_TRUE(settings.rename.enabled);
  EXPECT_TRUE(settings.rename.rules.empty());
}

// The prefs are what a restart keeps: the switches, the off sites and the rules
// (in order) come back as they were written.
TEST(TabModelPrefsTest, TheTitleSettingsComeBackAsTheyWereWritten) {
  TestingPrefServiceSimple prefs;
  RegisterProfilePrefs(prefs.registry());
  RenameRule first;
  first.id = "a";
  first.name = "First";
  first.match.type = RuleType::kDomain;
  first.match.value = "example.com";
  first.text = "One";
  RenameRule second = first;
  second.id = "b";
  second.match.type = RuleType::kTitleContains;
  second.match.value = "needle";
  second.enabled = false;
  prefs.SetList(kRenameRules, RenameRulesToList({first, second}));
  prefs.SetBoolean(kRenameRulesEnabled, false);
  prefs.SetBoolean(kTidyTitles, false);
  base::ListValue sites;
  sites.Append("example.org");
  prefs.SetList(kTidyOffSites, std::move(sites));

  const TitleSettings settings = GetTitleSettings(prefs);
  EXPECT_FALSE(settings.tidy_enabled);
  EXPECT_FALSE(settings.rename.enabled);
  ASSERT_EQ(settings.tidy_off_sites.size(), 1u);
  EXPECT_EQ(settings.tidy_off_sites[0], "example.org");
  ASSERT_EQ(settings.rename.rules.size(), 2u);
  EXPECT_EQ(settings.rename.rules[0].id, "a");
  EXPECT_EQ(settings.rename.rules[1].id, "b");
  EXPECT_FALSE(settings.rename.rules[1].enabled);
}

TEST(TabModelPrefsTest, JunkInTheRulesPrefIsDroppedOnLoad) {
  TestingPrefServiceSimple prefs;
  RegisterProfilePrefs(prefs.registry());
  base::ListValue junk;
  junk.Append(5);
  junk.Append("text");
  junk.Append(base::DictValue());
  prefs.SetList(kRenameRules, std::move(junk));
  EXPECT_TRUE(GetTitleSettings(prefs).rename.rules.empty());
}

}  // namespace aerie_tab_model
