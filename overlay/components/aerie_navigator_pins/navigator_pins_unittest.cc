// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_navigator_pins/navigator_pins.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace aerie_navigator_pins {
namespace {

TEST(NavigatorPinsTest, PinsOnlyWithBothFeaturesAndAToken) {
  EXPECT_TRUE(ShouldPinNavigator({true, true, true}));
  EXPECT_FALSE(ShouldPinNavigator({false, true, true}));
  EXPECT_FALSE(ShouldPinNavigator({true, false, true}));
}

TEST(NavigatorPinsTest, NoTokenMeansProtectionIsOffSoTheRealValuesShow) {
  // The browser sends no token when protection is off for the browser or the
  // site (S6).
  EXPECT_FALSE(ShouldPinNavigator({true, true, false}));
}

TEST(NavigatorPinsTest, TheBatteryIsChargingAndFull) {
  EXPECT_TRUE(kBatteryCharging);
  EXPECT_EQ(kBatteryLevel, 1.0);
  EXPECT_EQ(kBatteryChargingTimeSeconds, 0.0);
}

TEST(NavigatorPinsTest, AnEmptyVoiceListStaysEmpty) {
  EXPECT_EQ(VoiceIndexToKeep(0, std::nullopt), std::nullopt);
  EXPECT_EQ(VoiceIndexToKeep(0, 0), std::nullopt);
}

TEST(NavigatorPinsTest, TheSystemDefaultVoiceIsKept) {
  EXPECT_EQ(VoiceIndexToKeep(5, 3), std::optional<size_t>(3));
  EXPECT_EQ(VoiceIndexToKeep(1, 0), std::optional<size_t>(0));
}

TEST(NavigatorPinsTest, WithNoDefaultTheFirstVoiceIsKept) {
  EXPECT_EQ(VoiceIndexToKeep(5, std::nullopt), std::optional<size_t>(0));
}

TEST(NavigatorPinsTest, ADefaultIndexPastTheEndIsIgnored) {
  EXPECT_EQ(VoiceIndexToKeep(2, 7), std::optional<size_t>(0));
}

}  // namespace
}  // namespace aerie_navigator_pins
