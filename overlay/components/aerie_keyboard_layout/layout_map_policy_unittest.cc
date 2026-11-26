// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_keyboard_layout/layout_map_policy.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace aerie_keyboard_layout {

TEST(AerieKeyboardLayoutTest, ProtectedSiteRefusesTheLayoutMap) {
  EXPECT_TRUE(ShouldRefuseLayoutMap({.site_protected = true}));
}

TEST(AerieKeyboardLayoutTest, ProtectionOffGlobalOrForTheSiteGivesTheRealMap) {
  EXPECT_FALSE(ShouldRefuseLayoutMap({.site_protected = false}));
}

}  // namespace aerie_keyboard_layout
