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

constexpr bool kRegular = false;
constexpr bool kIncognito = true;

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
  EXPECT_TRUE(ShouldPrompt(&prefs_, "example.org", kRegular));
  RecordDecline(&prefs_, "example.org");
  EXPECT_FALSE(ShouldPrompt(&prefs_, "example.org", kRegular));
  EXPECT_TRUE(ShouldPrompt(&prefs_, "other.example", kRegular));
}

TEST_F(WidevineOptInTest, DecliningTwiceKeepsOneEntry) {
  RecordDecline(&prefs_, "example.org");
  RecordDecline(&prefs_, "example.org");
  EXPECT_EQ(1u, prefs_.GetList(kDeclinedSites).size());
}

TEST_F(WidevineOptInTest, NeverPromptsWhenOptedInOrWithoutAHost) {
  EXPECT_FALSE(ShouldPrompt(&prefs_, "", kRegular));
  SetEnabled(&prefs_, true);
  EXPECT_FALSE(ShouldPrompt(&prefs_, "example.org", kRegular));
}

TEST_F(WidevineOptInTest, OffTheRecordDeclinesStayOutOfLocalState) {
  RecordDecline(&prefs_, "example.org", /*off_the_record=*/true);
  EXPECT_TRUE(prefs_.GetList(kDeclinedSites).empty());
  EXPECT_TRUE(IsDeclined(&prefs_, "example.org", kIncognito));
  EXPECT_FALSE(ShouldPrompt(&prefs_, "example.org", kIncognito));
  EXPECT_TRUE(ShouldPrompt(&prefs_, "other.example", kIncognito));
  ClearSessionDeclinesForTesting();
  EXPECT_TRUE(ShouldPrompt(&prefs_, "example.org", kIncognito));
}

TEST_F(WidevineOptInTest, AnIncognitoDeclineDoesNotReachRegularWindows) {
  RecordDecline(&prefs_, "example.org", /*off_the_record=*/true);
  EXPECT_FALSE(IsDeclined(&prefs_, "example.org", kRegular));
  EXPECT_TRUE(ShouldPrompt(&prefs_, "example.org", kRegular));
  // It still holds in incognito.
  EXPECT_FALSE(ShouldPrompt(&prefs_, "example.org", kIncognito));
}

TEST_F(WidevineOptInTest, ARegularDeclineAlsoHoldsInIncognito) {
  RecordDecline(&prefs_, "example.org");
  EXPECT_TRUE(IsDeclined(&prefs_, "example.org", kRegular));
  EXPECT_TRUE(IsDeclined(&prefs_, "example.org", kIncognito));
  EXPECT_FALSE(ShouldPrompt(&prefs_, "example.org", kIncognito));
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

TEST_F(WidevineOptInTest, OnlyWidevineMayReachGoogleTheUpdateServer) {
  const char kCrlSet[] = "hfnkpimlhhgieaddgfemjhofmfblmnib";
  const GURL dummy("https://dummy.invalid/com");
  EXPECT_EQ(dummy, WidevineUpdateUrl(&prefs_, dummy));  // off: the fallback
  // Not Google's server: nothing is held back.
  EXPECT_TRUE(ShouldSendToUpdateServer(kCrlSet, dummy));
  EXPECT_TRUE(ShouldSendToUpdateServer(kWidevineAppId, dummy));
  SetEnabled(&prefs_, true);
  const GURL real = WidevineUpdateUrl(&prefs_, dummy);
  EXPECT_TRUE(ShouldSendToUpdateServer(kWidevineAppId, real));
  EXPECT_FALSE(ShouldSendToUpdateServer(kCrlSet, real));
  EXPECT_FALSE(ShouldSendToUpdateServer("", real));
}

// The URL was read when the check started; the pref may flip before the
// request is built. The decision follows the URL, not the pref.
TEST_F(WidevineOptInTest, TheDecisionFollowsTheUrlNotThePref) {
  const char kCrlSet[] = "hfnkpimlhhgieaddgfemjhofmfblmnib";
  SetEnabled(&prefs_, true);
  const GURL real = WidevineUpdateUrl(&prefs_, GURL("https://dummy.invalid/com"));
  SetEnabled(&prefs_, false);
  EXPECT_FALSE(ShouldSendToUpdateServer(kCrlSet, real));
  EXPECT_TRUE(ShouldSendToUpdateServer(kWidevineAppId, real));
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
