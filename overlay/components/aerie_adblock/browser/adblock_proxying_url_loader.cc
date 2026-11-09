// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_adblock/browser/adblock_proxying_url_loader.h"

#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "base/metrics/histogram_functions.h"
#include "components/aerie_adblock/browser/request_classifier.h"
#include "mojo/public/cpp/bindings/message.h"
#include "mojo/public/cpp/bindings/self_owned_receiver.h"
#include "net/base/net_errors.h"
#include "net/url_request/redirect_info.h"
#include "services/network/public/cpp/url_loader_completion_status.h"
#include "services/network/public/mojom/early_hints.mojom.h"

namespace aerie_adblock {

// static
void AdblockProxyingURLLoader::CreateAndStart(
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
    content::GlobalRenderFrameHostId frame_id,
    SessionKey session) {
  std::unique_ptr<AdblockProxyingURLLoader> loader(new AdblockProxyingURLLoader(
      request, std::move(client), factory_initiator, factory_isolation_info,
      std::move(service), frame_id, session));
  loader->Start(target_factory, request_id, options, traffic_annotation);
  // Owned by the renderer's pipe: deleted when the renderer closes it.
  mojo::MakeSelfOwnedReceiver(std::move(loader), std::move(loader_receiver));
}

AdblockProxyingURLLoader::AdblockProxyingURLLoader(
    const network::ResourceRequest& request,
    mojo::PendingRemote<network::mojom::URLLoaderClient> client,
    const url::Origin& factory_initiator,
    const net::IsolationInfo& factory_isolation_info,
    base::WeakPtr<FilterEngineService> service,
    content::GlobalRenderFrameHostId frame_id,
    SessionKey session)
    : request_(request),
      factory_initiator_(factory_initiator),
      factory_isolation_info_(factory_isolation_info),
      service_(std::move(service)),
      frame_id_(frame_id),
      session_(session),
      forwarding_client_(std::move(client)) {}

AdblockProxyingURLLoader::~AdblockProxyingURLLoader() = default;

void AdblockProxyingURLLoader::Start(
    network::mojom::URLLoaderFactory* target_factory,
    int32_t request_id,
    uint32_t options,
    const net::MutableNetworkTrafficAnnotationTag& traffic_annotation) {
  target_factory->CreateLoaderAndStart(
      loader_.BindNewPipeAndPassReceiver(), request_id, options, request_,
      client_receiver_.BindNewPipeAndPassRemote(), traffic_annotation);
  client_receiver_.set_disconnect_handler(base::BindOnce(
      &AdblockProxyingURLLoader::OnNetworkConnectionError,
      base::Unretained(this)));
}

void AdblockProxyingURLLoader::FollowRedirect(
    network::HttpRequestHeadersUpdateParams headers_update_params,
    const std::optional<GURL>& new_url) {
  if (!NetworkUsable()) {
    // The request already ended (the network completed it, or went away) and
    // the renderer's answer raced that. Not a bad message, and nothing to
    // follow: calling the network loader now would use a dead pipe.
    return;
  }
  if (!redirect_pending_) {
    mojo::ReportBadMessage("Unexpected FollowRedirect");
    return;
  }
  redirect_pending_ = false;

  // The URL the next request will really use: the client may name one (a
  // same-origin rewrite), else it is the redirect's.
  GURL target = new_url.has_value() ? *new_url : *pending_redirect_url_;
  pending_redirect_url_.reset();

  // Judge the hop with the original request's context.
  network::ResourceRequest hop = request_;
  hop.url = target;
  std::optional<ClassifiedRequest> classified =
      ClassifyRequest(hop, factory_initiator_, factory_isolation_info_);
  if (classified.has_value()) {
    classified->session = session_;
  } else {
    // Not something the engine judges (for example a non-http scheme).
    loader_->FollowRedirect(std::move(headers_update_params), new_url);
    return;
  }
  if (!service_) {
    FailRequest(net::ERR_ABORTED);
    return;
  }
  service_->ShouldBlock(
      std::move(*classified),
      base::BindOnce(&AdblockProxyingURLLoader::OnRedirectVerdict,
                     weak_factory_.GetWeakPtr(), std::move(headers_update_params),
                     new_url, target));
}

void AdblockProxyingURLLoader::OnRedirectVerdict(
    network::HttpRequestHeadersUpdateParams headers_update_params,
    std::optional<GURL> new_url,
    GURL blocked_target,
    bool should_block) {
  base::UmaHistogramBoolean("Aerie.Adblock.RequestBlocked", should_block);
  base::UmaHistogramBoolean("Aerie.Adblock.RedirectHopBlocked", should_block);
  if (!NetworkUsable()) {
    // The request already ended (the network completed it, or went away) while
    // the verdict was pending. The client has its completion or disconnect, and
    // must not get a second one.
    return;
  }
  if (should_block) {
    if (service_) {
      service_->RecordBlocked(frame_id_, blocked_target);
    }
    FailRequest(net::ERR_BLOCKED_BY_CLIENT);
    return;
  }
  loader_->FollowRedirect(std::move(headers_update_params), new_url);
}

bool AdblockProxyingURLLoader::NetworkUsable() const {
  return !completed_ && !network_gone_ && loader_.is_bound() && loader_.is_connected();
}

void AdblockProxyingURLLoader::FailRequest(int net_error) {
  // Cancel the network request by dropping both pipes to it, and tell the
  // client why. The client then closes its URLLoader pipe, which deletes this.
  network_gone_ = true;
  loader_.reset();
  client_receiver_.reset();
  if (!completed_ && forwarding_client_.is_bound()) {
    completed_ = true;
    forwarding_client_->OnComplete(network::URLLoaderCompletionStatus(net_error));
  }
}

void AdblockProxyingURLLoader::SetPriority(net::RequestPriority priority,
                                           int intra_priority_value) {
  if (loader_.is_bound()) {
    loader_->SetPriority(priority, intra_priority_value);
  }
}

void AdblockProxyingURLLoader::OnReceiveEarlyHints(
    network::mojom::EarlyHintsPtr early_hints) {
  forwarding_client_->OnReceiveEarlyHints(std::move(early_hints));
}

void AdblockProxyingURLLoader::OnReceiveResponse(
    network::mojom::URLResponseHeadPtr head,
    mojo::ScopedDataPipeConsumerHandle body,
    std::optional<mojo_base::BigBuffer> cached_metadata) {
  redirect_pending_ = false;
  forwarding_client_->OnReceiveResponse(std::move(head), std::move(body),
                                        std::move(cached_metadata));
}

void AdblockProxyingURLLoader::OnReceiveRedirect(
    const net::RedirectInfo& redirect_info,
    network::mojom::URLResponseHeadPtr head) {
  redirect_pending_ = true;
  pending_redirect_url_ = redirect_info.new_url;
  forwarding_client_->OnReceiveRedirect(redirect_info, std::move(head));
}

void AdblockProxyingURLLoader::OnUploadProgress(
    int64_t current_position,
    int64_t total_size,
    base::OnceCallback<void()> callback) {
  forwarding_client_->OnUploadProgress(current_position, total_size,
                                       std::move(callback));
}

void AdblockProxyingURLLoader::OnTransferSizeUpdated(
    int32_t transfer_size_diff) {
  forwarding_client_->OnTransferSizeUpdated(transfer_size_diff);
}

void AdblockProxyingURLLoader::OnComplete(
    const network::URLLoaderCompletionStatus& status) {
  redirect_pending_ = false;
  completed_ = true;
  forwarding_client_->OnComplete(status);
}

void AdblockProxyingURLLoader::OnNetworkConnectionError() {
  // The network side closed. Drop our end of its loader (a verdict that comes
  // later then sees it is gone), and let the client know by dropping our end,
  // which makes it close the loader pipe and so deletes this object.
  network_gone_ = true;
  loader_.reset();
  forwarding_client_.reset();
}

}  // namespace aerie_adblock
