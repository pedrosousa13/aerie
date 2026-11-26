// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_media_devices/device_list.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace aerie_media_devices {
namespace {

// A page that holds no permission, on a site where protection is on.
constexpr Policy kProtected = {.master_enabled = true,
                               .site_protected = true,
                               .has_permission = false};

TEST(DeviceListPolicyTest, PinsTheListWithoutPermissionWhenProtected) {
  EXPECT_TRUE(ShouldPinDeviceList(kProtected));
}

TEST(DeviceListPolicyTest, RealDevicesAfterAGrant) {
  Policy policy = kProtected;
  policy.has_permission = true;
  EXPECT_FALSE(ShouldPinDeviceList(policy));
}

TEST(DeviceListPolicyTest, RealListWhenProtectionIsOffForTheSite) {
  // The browser reports "not protected" when the global switch is off or the
  // site has its own off.
  Policy policy = kProtected;
  policy.site_protected = false;
  EXPECT_FALSE(ShouldPinDeviceList(policy));
}

TEST(DeviceListPolicyTest, RealListWhenHeliumNoiseIsOff) {
  Policy policy = kProtected;
  policy.master_enabled = false;
  EXPECT_FALSE(ShouldPinDeviceList(policy));
}

TEST(DeviceListTest, PinnedListHasExactlyOneEntryPerKind) {
  EXPECT_EQ(kPinnedEntriesPerKind, 1);
}

TEST(DeviceListTest, StockListWithoutPermissionNeverHasMoreThanOne) {
  // Chromium already returns at most one entry per kind when there is no
  // permission, so the pinned list only ever adds the missing entry.
  EXPECT_EQ(MissingPinnedEntries(0), 1);
  EXPECT_EQ(MissingPinnedEntries(1), 0);
  EXPECT_EQ(MissingPinnedEntries(2), 0);
}

}  // namespace
}  // namespace aerie_media_devices
