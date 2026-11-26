// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_media_devices/device_list.h"

namespace aerie_media_devices {

bool ShouldPinDeviceList(const Policy& policy) {
  return policy.master_enabled && policy.site_protected &&
         !policy.has_permission;
}

int MissingPinnedEntries(int stock_count) {
  const int have = stock_count < 0 ? 0 : stock_count;
  return have < kPinnedEntriesPerKind ? kPinnedEntriesPerKind - have : 0;
}

}  // namespace aerie_media_devices
