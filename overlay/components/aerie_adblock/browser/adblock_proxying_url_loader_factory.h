// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_ADBLOCK_BROWSER_ADBLOCK_PROXYING_URL_LOADER_FACTORY_H_
#define COMPONENTS_AERIE_ADBLOCK_BROWSER_ADBLOCK_PROXYING_URL_LOADER_FACTORY_H_

#include <cstdint>

#include "base/memory/weak_ptr.h"
#include "content/public/browser/global_routing_id.h"
#include "components/aerie_adblock/browser/filter_engine_service.h"
#include "content/public/browser/content_browser_client.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "net/base/isolation_info.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/self_deleting_url_loader_factory.h"
#include "services/network/public/cpp/url_loader_factory_builder.h"
#include "services/network/public/mojom/url_loader.mojom.h"
#include "services/network/public/mojom/url_loader_factory.mojom.h"
#include "url/origin.h"

namespace aerie_adblock {

// Sits in front of the network for one URLLoaderFactory and fails the requests
// the Filter Engine says to block, with net::ERR_BLOCKED_BY_CLIENT.
//
// It runs in the browser process, on the thread that creates it (the UI
// thread). The renderer holds only the other end of the Mojo pipe, so a
// compromised renderer cannot skip, change or read the verdict. This is the
// same place Chromium puts the extensions' webRequest proxy.
//
// Requests it does not judge (see ClassifyRequest) go straight through, with
// no wrapper: that fast path costs one function call. Requests it judges wait
// for the engine, then fail, or go through an AdblockProxyingURLLoader that
// judges every redirect target too. Headers, cookies and the request itself
// are never changed.
class AdblockProxyingURLLoaderFactory
    : public network::SelfDeletingURLLoaderFactory {
 public:
  AdblockProxyingURLLoaderFactory(
      mojo::PendingReceiver<network::mojom::URLLoaderFactory> loader_receiver,
      mojo::PendingRemote<network::mojom::URLLoaderFactory>
          target_factory_remote,
      const url::Origin& request_initiator,
      const net::IsolationInfo& isolation_info,
      base::WeakPtr<FilterEngineService> service,
      base::SelfDeletingPassKey pass_key);

  // The frame this factory was made for (a document's or a navigation's
  // factory; workers have none). Blocked requests count toward that frame's
  // tab (page_block_counter.h). Set once, right after creation: the
  // constructor's last parameter must stay the SelfDeletingPassKey.
  void SetFrameId(content::GlobalRenderFrameHostId frame_id) {
    frame_id_ = frame_id;
  }

  AdblockProxyingURLLoaderFactory(const AdblockProxyingURLLoaderFactory&) =
      delete;
  AdblockProxyingURLLoaderFactory& operator=(
      const AdblockProxyingURLLoaderFactory&) = delete;

  // Which factory types this proxy covers.
  static bool IsCoveredType(
      content::ContentBrowserClient::URLLoaderFactoryType type);

  // network::mojom::URLLoaderFactory:
  void CreateLoaderAndStart(
      mojo::PendingReceiver<network::mojom::URLLoader> loader_receiver,
      int32_t request_id,
      uint32_t options,
      const network::ResourceRequest& request,
      mojo::PendingRemote<network::mojom::URLLoaderClient> client,
      const net::MutableNetworkTrafficAnnotationTag& traffic_annotation)
      override;

 private:
  ~AdblockProxyingURLLoaderFactory() override;

  void OnVerdict(
      mojo::PendingReceiver<network::mojom::URLLoader> loader_receiver,
      int32_t request_id,
      uint32_t options,
      network::ResourceRequest request,
      mojo::PendingRemote<network::mojom::URLLoaderClient> client,
      net::MutableNetworkTrafficAnnotationTag traffic_annotation,
      bool should_block);

  void OnTargetFactoryError();

  mojo::Remote<network::mojom::URLLoaderFactory> target_factory_;
  const url::Origin request_initiator_;
  const net::IsolationInfo isolation_info_;
  // The browser owns the service and may destroy it at shutdown while
  // factories live on. Requests that arrive then are aborted.
  base::WeakPtr<FilterEngineService> service_;
  // The frame this factory was made for (invalid for workers), for the
  // per-page blocked count.
  content::GlobalRenderFrameHostId frame_id_;
  base::WeakPtrFactory<AdblockProxyingURLLoaderFactory> weak_factory_{this};
};

}  // namespace aerie_adblock

#endif  // COMPONENTS_AERIE_ADBLOCK_BROWSER_ADBLOCK_PROXYING_URL_LOADER_FACTORY_H_
