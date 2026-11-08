// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/aerie_adblock/aerie_adblock_main_extra_parts.h"

#include "base/check.h"
#include "base/files/file_path.h"
#include "base/path_service.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/net/system_network_context_manager.h"
#include "chrome/common/chrome_paths.h"
#include "components/aerie_adblock/browser/aerie_adblock_hook.h"
#include "components/aerie_adblock/browser/aerie_protection_hook.h"
#include "components/aerie_adblock/browser/filter_engine_service.h"
#include "components/aerie_adblock/browser/filter_list_manager.h"
#include "components/aerie_adblock/browser/protection_service.h"

AerieAdblockMainExtraParts::AerieAdblockMainExtraParts() = default;

AerieAdblockMainExtraParts::~AerieAdblockMainExtraParts() = default;

void AerieAdblockMainExtraParts::PreProfileInit() {
  CHECK(!service_);
  service_ = aerie_adblock::CreateAndStartFilterEngineService();

  // The list settings are shared by every profile, like the engine, so they
  // live in the user data directory. Lists are fetched through the system
  // network context: no profile, no cookies, no proxy extension.
  base::FilePath user_data_dir;
  CHECK(base::PathService::Get(chrome::DIR_USER_DATA, &user_data_dir));
  list_manager_ = aerie_adblock::CreateAndInitFilterListManager(
      *service_, user_data_dir.AppendASCII("AerieFilterLists"),
      g_browser_process->system_network_context_manager()
          ->GetSharedURLLoaderFactory());

  CHECK(!protection_service_);
  protection_service_ = aerie_adblock::CreateAndStartProtectionService();
}

void AerieAdblockMainExtraParts::PostMainMessageLoopRun() {
  // Factories that outlive the service hold a WeakPtr and abort new requests.
  list_manager_.reset();
  service_.reset();
  protection_service_.reset();
}
