// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_ADBLOCK_BROWSER_ADBLOCK_PROXYING_URL_LOADER_H_
#define COMPONENTS_AERIE_ADBLOCK_BROWSER_ADBLOCK_PROXYING_URL_LOADER_H_

#include <cstdint>
#include <optional>

#include "base/memory/weak_ptr.h"
#include "components/aerie_adblock/browser/filter_engine_service.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "content/public/browser/global_routing_id.h"
#include "net/base/isolation_info.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/http_request_headers_update_params.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/mojom/url_loader.mojom.h"
#include "services/network/public/mojom/url_loader_factory.mojom.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace aerie_adblock {

// Stands between the renderer and the network service for ONE request that the
// Filter Engine judged and allowed, so that every redirect target can be
// judged as well, before the hop goes out.
//
// A redirect is followed in two steps: the network service tells the client
// (OnReceiveRedirect), and the client answers with FollowRedirect(). The
// request for the new URL only starts when FollowRedirect() reaches the
// network service, and it reaches it through this class. So this class judges
// the new URL when FollowRedirect() comes in, with the same top-level-site
// context as the first URL (the copy of the original request keeps its
// trusted IsolationInfo), and forwards or fails there:
//   - blocked: the client gets OnComplete(ERR_BLOCKED_BY_CLIENT), and the
//     network request is cancelled (the pipes to it are dropped);
//   - the engine is gone (browser shutdown): ERR_ABORTED;
//   - otherwise FollowRedirect() goes on to the network service.
//
// Everything else is forwarded as it comes: SetPriority() to the network
// loader, and every URLLoaderClient call (early hints, response with its body
// pipe, redirect, upload progress with its ack, transfer size, completion) to
// the renderer's client. The URLLoader interface of this Chromium has no
// pause or resume calls (checked in url_loader.mojom).
//
// The object owns itself through the Mojo receiver of the renderer's URLLoader
// pipe: when the renderer closes it, this object is deleted.
class AdblockProxyingURLLoader : public network::mojom::URLLoader,
                                 public network::mojom::URLLoaderClient {
 public:
  // Starts the request on |target_factory| and binds |loader_receiver|. The
  // object deletes itself when |loader_receiver| disconnects.
  static void CreateAndStart(
      network::mojom::URLLoaderFactory* target_factory,
      mojo::PendingReceiver<network::mojom::URLLoader> loader_receiver,
      int32_t request_id,
      uint32_t options,
      const network::ResourceRequest& request,
      mojo::PendingRemote<network::mojom::URLLoaderClient> client,
      const net::MutableNetworkTrafficAnnotationTag& traffic_annotation,
      const url::Origin& factory_initiator,
      const net::IsolationInfo& factory_isolation_info,
      base::WeakPtr<FilterEngineService> service,
      content::GlobalRenderFrameHostId frame_id =
          content::GlobalRenderFrameHostId());

  AdblockProxyingURLLoader(const AdblockProxyingURLLoader&) = delete;
  AdblockProxyingURLLoader& operator=(const AdblockProxyingURLLoader&) = delete;
  ~AdblockProxyingURLLoader() override;

 private:
  AdblockProxyingURLLoader(
      const network::ResourceRequest& request,
      mojo::PendingRemote<network::mojom::URLLoaderClient> client,
      const url::Origin& factory_initiator,
      const net::IsolationInfo& factory_isolation_info,
      base::WeakPtr<FilterEngineService> service,
      content::GlobalRenderFrameHostId frame_id);

  void Start(network::mojom::URLLoaderFactory* target_factory,
             int32_t request_id,
             uint32_t options,
             const net::MutableNetworkTrafficAnnotationTag& traffic_annotation);

  // network::mojom::URLLoader:
  void FollowRedirect(
      network::HttpRequestHeadersUpdateParams headers_update_params,
      const std::optional<GURL>& new_url) override;
  void SetPriority(net::RequestPriority priority,
                   int intra_priority_value) override;

  // network::mojom::URLLoaderClient:
  void OnReceiveEarlyHints(network::mojom::EarlyHintsPtr early_hints) override;
  void OnReceiveResponse(
      network::mojom::URLResponseHeadPtr head,
      mojo::ScopedDataPipeConsumerHandle body,
      std::optional<mojo_base::BigBuffer> cached_metadata) override;
  void OnReceiveRedirect(const net::RedirectInfo& redirect_info,
                         network::mojom::URLResponseHeadPtr head) override;
  void OnUploadProgress(int64_t current_position,
                        int64_t total_size,
                        base::OnceCallback<void()> callback) override;
  void OnTransferSizeUpdated(int32_t transfer_size_diff) override;
  void OnComplete(const network::URLLoaderCompletionStatus& status) override;

  void OnRedirectVerdict(
      network::HttpRequestHeadersUpdateParams headers_update_params,
      std::optional<GURL> new_url,
      bool should_block);
  // True while the network loader can still be talked to. Remote::is_connected() must
  // not be called on an unbound remote (it DCHECKs, and in official builds reports true), so
  // the bound check comes first and a flag covers the window before the disconnect is seen.
  bool NetworkUsable() const;
  void FailRequest(int net_error);
  void OnNetworkConnectionError();

  // The request as the renderer made it. Redirect targets are judged as this
  // request with another URL, so they get the same first party context.
  const network::ResourceRequest request_;
  const url::Origin factory_initiator_;
  const net::IsolationInfo factory_isolation_info_;
  base::WeakPtr<FilterEngineService> service_;
  // The frame the request came from, for the per-page blocked count.
  const content::GlobalRenderFrameHostId frame_id_;

  mojo::Remote<network::mojom::URLLoader> loader_;
  mojo::Remote<network::mojom::URLLoaderClient> forwarding_client_;
  mojo::Receiver<network::mojom::URLLoaderClient> client_receiver_{this};

  // The URL the network service asked about, until the client answers.
  std::optional<GURL> pending_redirect_url_;
  bool redirect_pending_ = false;
  // The network side has delivered OnComplete (or the loader failed it). A client
  // gets exactly one: a verdict that arrives later must not send another.
  bool completed_ = false;
  // The network side closed its pipe (OnNetworkConnectionError), or this class cancelled it.
  bool network_gone_ = false;

  base::WeakPtrFactory<AdblockProxyingURLLoader> weak_factory_{this};
};

}  // namespace aerie_adblock

#endif  // COMPONENTS_AERIE_ADBLOCK_BROWSER_ADBLOCK_PROXYING_URL_LOADER_H_
