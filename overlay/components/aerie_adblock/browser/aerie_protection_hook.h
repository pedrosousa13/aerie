// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_ADBLOCK_BROWSER_AERIE_PROTECTION_HOOK_H_
#define COMPONENTS_AERIE_ADBLOCK_BROWSER_AERIE_PROTECTION_HOOK_H_

#include <memory>

#include "components/aerie_adblock/browser/protection_service.h"

namespace aerie_adblock {

// The command line switch that adds one more list of domains (one per line) to
// the protection list. For the Bench: it lists a fixture domain. It only adds
// blocked domains and never removes any.
inline constexpr char kTestProtectionListSwitch[] =
    "aerie-test-protection-list";

// Creates the browser's protection service, registers it as the process
// instance and starts loading the shipped list (and the test list, if the
// switch is given). The caller owns it: see chrome/browser/aerie_adblock. UI
// thread, after the resource bundle and the thread pool exist.
std::unique_ptr<ProtectionService> CreateAndStartProtectionService();

}  // namespace aerie_adblock

#endif  // COMPONENTS_AERIE_ADBLOCK_BROWSER_AERIE_PROTECTION_HOOK_H_
