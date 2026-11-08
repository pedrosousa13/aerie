// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_adblock/browser/aerie_protection_hook.h"

#include <string_view>

#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/task/thread_pool.h"
#include "components/aerie_adblock/browser/aerie_adblock_switches.h"
#include "components/aerie_adblock/resources/grit/aerie_adblock_resources.h"
#include "ui/base/resource/resource_bundle.h"

namespace aerie_adblock {

namespace {

// The serialized protection engine that the build compiled from the pinned
// phishing list. The resource bundle's data stays mapped for the life of the
// process.
std::string_view ShippedProtectionEngine() {
  return ui::ResourceBundle::GetSharedInstance().GetRawDataResource(
      IDR_AERIE_PROTECTION_ENGINE);
}

}  // namespace

std::unique_ptr<ProtectionService> CreateAndStartProtectionService() {
  auto service = std::make_unique<ProtectionService>(
      base::ThreadPool::CreateSequencedTaskRunner(
          {base::MayBlock(), base::TaskPriority::USER_BLOCKING,
           base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN}));
  service->SetAsProcessInstance();
  service->Start(
      ShippedProtectionEngine(),
      base::CommandLine::ForCurrentProcess()->GetSwitchValuePath(
          switches::kTestProtectionListSwitch));
  return service;
}

}  // namespace aerie_adblock
