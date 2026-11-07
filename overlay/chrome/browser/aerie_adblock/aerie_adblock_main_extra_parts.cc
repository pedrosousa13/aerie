// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/aerie_adblock/aerie_adblock_main_extra_parts.h"

#include "base/check.h"
#include "components/aerie_adblock/browser/aerie_adblock_hook.h"
#include "components/aerie_adblock/browser/filter_engine_service.h"

AerieAdblockMainExtraParts::AerieAdblockMainExtraParts() = default;

AerieAdblockMainExtraParts::~AerieAdblockMainExtraParts() = default;

void AerieAdblockMainExtraParts::PreProfileInit() {
  CHECK(!service_);
  service_ = aerie_adblock::CreateAndStartFilterEngineService();
}

void AerieAdblockMainExtraParts::PostMainMessageLoopRun() {
  // Factories that outlive the service hold a WeakPtr and abort new requests.
  service_.reset();
}
