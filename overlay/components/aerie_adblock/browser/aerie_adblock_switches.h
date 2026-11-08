// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_ADBLOCK_BROWSER_AERIE_ADBLOCK_SWITCHES_H_
#define COMPONENTS_AERIE_ADBLOCK_BROWSER_AERIE_ADBLOCK_SWITCHES_H_

namespace aerie_adblock::switches {

// Lets filter list addresses use http for 127.0.0.1, ::1 and localhost, and
// for nothing else. The Bench needs it because its fixture server has no TLS;
// users never need it. Same idea as
// --aerie-test-allow-loopback-extension-proxy.
inline constexpr char kAllowLoopbackFilterLists[] =
    "aerie-test-allow-loopback-filter-lists";

// Adds one more list of domains (one per line) to the protection list. For the
// Bench: it lists a fixture domain. It only adds blocked domains and never
// removes any.
inline constexpr char kTestProtectionListSwitch[] = "aerie-test-protection-list";

}  // namespace aerie_adblock::switches

#endif  // COMPONENTS_AERIE_ADBLOCK_BROWSER_AERIE_ADBLOCK_SWITCHES_H_
