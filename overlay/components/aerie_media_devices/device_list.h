// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_MEDIA_DEVICES_DEVICE_LIST_H_
#define COMPONENTS_AERIE_MEDIA_DEVICES_DEVICE_LIST_H_

// The device list that navigator.mediaDevices.enumerateDevices() returns to a
// page that holds no camera or microphone permission, while fingerprinting
// protection is on (issue #202). The list is one audioinput, one audiooutput
// and one videoinput, each with an empty deviceId, label and groupId, whatever
// the machine has. Pure code: no Blink, no content, no base, so it has unit
// tests. The media devices manager only decides when to call it
// (patches/hardening/0028).
namespace aerie_media_devices {

// How many entries of each kind a pinned list has.
inline constexpr int kPinnedEntriesPerKind = 1;

// What decides whether a request gets the pinned list.
struct Policy {
  // The HeliumNoise feature.
  bool master_enabled;
  // Whether protection is on for the site of the requesting page: the global
  // switch is on and the site has no per-site off. The browser decides this
  // with ContentBrowserClient::HeliumNoiseEnabled.
  bool site_protected;
  // Whether the page holds a permission for this kind of device.
  bool has_permission;
};

// A page with a grant for a kind of device, or with protection off (globally or
// for the site), gets the real list for that kind.
bool ShouldPinDeviceList(const Policy& policy);

// How many empty entries to add to a kind whose list has `stock_count` entries
// so that it reaches kPinnedEntriesPerKind. Chromium already trims a list
// without permission to one entry, so this is 1 for an empty list (the machine
// has no such device) and 0 otherwise.
int MissingPinnedEntries(int stock_count);

}  // namespace aerie_media_devices

#endif  // COMPONENTS_AERIE_MEDIA_DEVICES_DEVICE_LIST_H_
