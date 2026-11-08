// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_adblock/browser/aerie_adblock_hook.h"

#include <string_view>
#include <utility>

#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/notreached.h"
#include "base/memory/self_deleting.h"
#include "base/task/thread_pool.h"
#include "content/public/browser/render_frame_host.h"
#include "components/aerie_adblock/browser/adblock_proxying_url_loader_factory.h"
#include "components/aerie_adblock/browser/aerie_adblock_switches.h"
#include "components/aerie_adblock/browser/filter_engine_service.h"
#include "components/aerie_adblock/browser/filter_list_state.h"
#include "components/aerie_adblock/browser/page_block_counter.h"
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

// The text of a built-in list, from the resource bundle (decompressed).
// An id that is not a built-in list has no shipped text: NOTREACHED in debug
// builds, and an empty text otherwise, which the manager's list check rejects,
// so an unknown id never silently gets another list's rules.
std::string ShippedList(std::string_view id) {
  int resource;
  if (id == kEasyListId) {
    resource = IDR_AERIE_ADBLOCK_EASYLIST;
  } else if (id == kEasyPrivacyId) {
    resource = IDR_AERIE_ADBLOCK_EASYPRIVACY;
  } else {
    NOTREACHED() << "no shipped text for list " << id;
  }
  return ui::ResourceBundle::GetSharedInstance().LoadDataResourceString(
      resource);
}

}  // namespace

std::unique_ptr<FilterEngineService> CreateAndStartFilterEngineService() {
  auto service = std::make_unique<FilterEngineService>(
      base::ThreadPool::CreateSequencedTaskRunner(
          {base::MayBlock(), base::TaskPriority::USER_BLOCKING,
           base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN}));
  service->SetAsProcessInstance();
  service->Start(ShippedEngine());
  // Blocked requests count toward their tab's page (the toolbar button).
  service->SetBlockedRequestCallback(
      base::BindRepeating(&PageBlockCounter::RecordBlocked));
  return service;
}

std::unique_ptr<FilterListManager> CreateAndInitFilterListManager(
    FilterEngineService& service,
    const base::FilePath& dir,
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory) {
  auto manager = std::make_unique<FilterListManager>(
      dir, service.GetWeakPtr(), std::move(url_loader_factory),
      base::BindRepeating(&ShippedList),
      base::CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kAllowLoopbackFilterLists));
  manager->SetAsProcessInstance();
  manager->Init();
  return manager;
}

void MaybeProxyURLLoaderFactory(
    content::ContentBrowserClient::URLLoaderFactoryType type,
    const url::Origin& request_initiator,
    const net::IsolationInfo& isolation_info,
    network::URLLoaderFactoryBuilder& factory_builder,
    content::RenderFrameHost* frame,
    content::BrowserContext* browser_context) {
  if (!AdblockProxyingURLLoaderFactory::IsCoveredType(type)) {
    return;
  }
  FilterEngineService* service = FilterEngineService::Get();
  if (!service) {
    return;
  }

  auto [receiver, remote] = factory_builder.Append();
  // The factory owns itself: it is deleted when its Mojo pipes close.
  AdblockProxyingURLLoaderFactory* proxy =
      base::MakeSelfDeleting<AdblockProxyingURLLoaderFactory>(
          std::move(receiver), std::move(remote), request_initiator,
          isolation_info, service->GetWeakPtr());
  proxy->SetSession(SessionKeyFor(browser_context));
  if (frame) {
    proxy->SetFrameId(frame->GetGlobalId());
  }
}

}  // namespace aerie_adblock
