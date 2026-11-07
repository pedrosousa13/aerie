// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_adblock/browser/aerie_adblock_hook.h"

#include <string_view>
#include <utility>

#include "base/memory/self_deleting.h"
#include "base/task/thread_pool.h"
#include "components/aerie_adblock/browser/adblock_proxying_url_loader_factory.h"
#include "components/aerie_adblock/browser/filter_engine_service.h"
#include "components/aerie_adblock/resources/grit/aerie_adblock_resources.h"
#include "ui/base/resource/resource_bundle.h"

namespace aerie_adblock {

namespace {

// The serialized engine that the build compiled from EasyList and EasyPrivacy.
// The resource bundle's data stays mapped for the life of the process.
std::string_view ShippedEngine() {
  return ui::ResourceBundle::GetSharedInstance().GetRawDataResource(
      IDR_AERIE_ADBLOCK_ENGINE);
}

}  // namespace

std::unique_ptr<FilterEngineService> CreateAndStartFilterEngineService() {
  auto service = std::make_unique<FilterEngineService>(
      base::ThreadPool::CreateSequencedTaskRunner(
          {base::MayBlock(), base::TaskPriority::USER_BLOCKING,
           base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN}));
  service->SetAsProcessInstance();
  service->Start(ShippedEngine());
  return service;
}

void MaybeProxyURLLoaderFactory(
    content::ContentBrowserClient::URLLoaderFactoryType type,
    const url::Origin& request_initiator,
    const net::IsolationInfo& isolation_info,
    network::URLLoaderFactoryBuilder& factory_builder) {
  if (!AdblockProxyingURLLoaderFactory::IsCoveredType(type)) {
    return;
  }
  FilterEngineService* service = FilterEngineService::Get();
  if (!service) {
    return;
  }

  auto [receiver, remote] = factory_builder.Append();
  // The factory owns itself: it is deleted when its Mojo pipes close.
  base::MakeSelfDeleting<AdblockProxyingURLLoaderFactory>(
      std::move(receiver), std::move(remote), request_initiator, isolation_info,
      service->GetWeakPtr());
}

}  // namespace aerie_adblock
