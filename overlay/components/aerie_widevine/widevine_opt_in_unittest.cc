// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_widevine/widevine_opt_in.h"

#include "base/functional/bind.h"
#include "components/prefs/testing_pref_service.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace aerie_widevine {

namespace {

class WidevineOptInTest : public testing::Test {
 protected:
  WidevineOptInTest() { RegisterLocalStatePrefs(prefs_.registry()); }
  ~WidevineOptInTest() override { ClearSessionDeclinesForTesting(); }

  TestingPrefServiceSimple prefs_;
};

TEST_F(WidevineOptInTest, OffByDefaultWithNoDeclinedSites) {
  EXPECT_FALSE(IsEnabled(&prefs_));
  EXPECT_TRUE(prefs_.GetList(kDeclinedSites).empty());
}

TEST_F(WidevineOptInTest, PromptsOnTheFirstRequestOnly) {
  EXPECT_TRUE(ShouldPrompt(&prefs_, "example.org"));
  RecordDecline(&prefs_, "example.org");
  EXPECT_FALSE(ShouldPrompt(&prefs_, "example.org"));
  EXPECT_TRUE(ShouldPrompt(&prefs_, "other.example"));
}

TEST_F(WidevineOptInTest, DecliningTwiceKeepsOneEntry) {
  RecordDecline(&prefs_, "example.org");
  RecordDecline(&prefs_, "example.org");
  EXPECT_EQ(1u, prefs_.GetList(kDeclinedSites).size());
}

TEST_F(WidevineOptInTest, NeverPromptsWhenOptedInOrWithoutAHost) {
  EXPECT_FALSE(ShouldPrompt(&prefs_, ""));
  SetEnabled(&prefs_, true);
  EXPECT_FALSE(ShouldPrompt(&prefs_, "example.org"));
}

TEST_F(WidevineOptInTest, OffTheRecordDeclinesStayOutOfLocalState) {
  RecordDecline(&prefs_, "example.org", /*off_the_record=*/true);
  EXPECT_TRUE(prefs_.GetList(kDeclinedSites).empty());
  EXPECT_TRUE(IsDeclined(&prefs_, "example.org"));
  EXPECT_FALSE(ShouldPrompt(&prefs_, "example.org"));
  EXPECT_TRUE(ShouldPrompt(&prefs_, "other.example"));
  ClearSessionDeclinesForTesting();
  EXPECT_TRUE(ShouldPrompt(&prefs_, "example.org"));
}

TEST_F(WidevineOptInTest, DecliningInANormalWindowStillPersistsAfterAnOtrDecline) {
  RecordDecline(&prefs_, "example.org", /*off_the_record=*/true);
  RecordDecline(&prefs_, "example.org");
  EXPECT_EQ(1u, prefs_.GetList(kDeclinedSites).size());
}

TEST_F(WidevineOptInTest, UpdateUrlIsTheFallbackUntilOptedIn) {
  const GURL fallback("https://dummy.invalid/com");
  EXPECT_EQ(fallback, WidevineUpdateUrl(&prefs_, fallback));
  EXPECT_EQ(fallback, WidevineUpdateUrl(nullptr, fallback));
  SetEnabled(&prefs_, true);
  EXPECT_NE(fallback, WidevineUpdateUrl(&prefs_, fallback));
  EXPECT_TRUE(WidevineUpdateUrl(&prefs_, fallback).SchemeIs("https"));
}

TEST_F(WidevineOptInTest, OnlyWidevineMayReachGoogleOnceOptedIn) {
  const char kCrlSet[] = "hfnkpimlhhgieaddgfemjhofmfblmnib";
  // Off: the request goes to the dummy origin, so nothing is held back.
  EXPECT_TRUE(ShouldSendToUpdateServer(&prefs_, kCrlSet));
  EXPECT_TRUE(ShouldSendToUpdateServer(nullptr, kCrlSet));
  SetEnabled(&prefs_, true);
  EXPECT_TRUE(ShouldSendToUpdateServer(&prefs_, kWidevineAppId));
  EXPECT_FALSE(ShouldSendToUpdateServer(&prefs_, kCrlSet));
  EXPECT_FALSE(ShouldSendToUpdateServer(&prefs_, ""));
}

TEST_F(WidevineOptInTest, RegistersOnlyAfterTheUserOptsIn) {
  int registered = 0;
  RegistrationGate gate(&prefs_,
                        base::BindRepeating([](int* n) { ++*n; }, &registered));
  EXPECT_EQ(0, registered);
  SetEnabled(&prefs_, true);
  EXPECT_EQ(1, registered);
  SetEnabled(&prefs_, false);
  SetEnabled(&prefs_, true);
  EXPECT_EQ(1, registered);
}

}  // namespace

}  // namespace aerie_widevine
