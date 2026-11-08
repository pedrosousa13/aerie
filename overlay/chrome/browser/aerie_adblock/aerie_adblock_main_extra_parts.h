// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_AERIE_ADBLOCK_AERIE_ADBLOCK_MAIN_EXTRA_PARTS_H_
#define CHROME_BROWSER_AERIE_ADBLOCK_AERIE_ADBLOCK_MAIN_EXTRA_PARTS_H_

#include <memory>

#include "chrome/browser/chrome_browser_main_extra_parts.h"

namespace aerie_adblock {
class FilterEngineService;
class FilterListManager;
class ProtectionService;
}  // namespace aerie_adblock

// Owns the browser's Filter Engine service, the filter list manager and the
// protection service. The services are created before any profile (so before
// any URLLoaderFactory for web content) and destroyed after the main message
// loop ends. Added in chrome_browser_main.cc by
// patches/adblock/0004-own-the-filter-engine-service.patch, in the same way as
// ChromeBrowserMainExtraPartsMemory.
class AerieAdblockMainExtraParts : public ChromeBrowserMainExtraParts {
 public:
  AerieAdblockMainExtraParts();
  AerieAdblockMainExtraParts(const AerieAdblockMainExtraParts&) = delete;
  AerieAdblockMainExtraParts& operator=(const AerieAdblockMainExtraParts&) =
      delete;
  ~AerieAdblockMainExtraParts() override;

  // ChromeBrowserMainExtraParts:
  void PreProfileInit() override;
  void PostMainMessageLoopRun() override;

 private:
  std::unique_ptr<aerie_adblock::FilterEngineService> service_;
  // Declared after the service: destroyed first (see PostMainMessageLoopRun).
  std::unique_ptr<aerie_adblock::FilterListManager> list_manager_;
  std::unique_ptr<aerie_adblock::ProtectionService> protection_service_;
};

#endif  // CHROME_BROWSER_AERIE_ADBLOCK_AERIE_ADBLOCK_MAIN_EXTRA_PARTS_H_
