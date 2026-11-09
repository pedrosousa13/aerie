// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_adblock/browser/adblock_proxying_url_loader_factory.h"

#include <optional>
#include <utility>

#include "base/functional/bind.h"
#include "base/metrics/histogram_functions.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/self_deleting.h"
#include "components/aerie_adblock/browser/adblock_proxying_url_loader.h"
#include "components/aerie_adblock/browser/request_classifier.h"
#include "net/base/net_errors.h"
#include "services/network/public/cpp/url_loader_completion_status.h"

namespace aerie_adblock {

AdblockProxyingURLLoaderFactory::AdblockProxyingURLLoaderFactory(
    mojo::PendingReceiver<network::mojom::URLLoaderFactory> loader_receiver,
    mojo::PendingRemote<network::mojom::URLLoaderFactory> target_factory_remote,
    const url::Origin& request_initiator,
    const net::IsolationInfo& isolation_info,
    base::WeakPtr<FilterEngineService> service,
    base::SelfDeletingPassKey pass_key)
    : network::SelfDeletingURLLoaderFactory(std::move(loader_receiver),
                                            pass_key),
      request_initiator_(request_initiator),
      isolation_info_(isolation_info),
      service_(std::move(service)) {
  target_factory_.Bind(std::move(target_factory_remote));
  target_factory_.set_disconnect_handler(base::BindOnce(
      &AdblockProxyingURLLoaderFactory::OnTargetFactoryError,
      base::Unretained(this)));
}

AdblockProxyingURLLoaderFactory::~AdblockProxyingURLLoaderFactory() = default;

// static
bool AdblockProxyingURLLoaderFactory::IsCoveredType(
    content::ContentBrowserClient::URLLoaderFactoryType type) {
  using Type = content::ContentBrowserClient::URLLoaderFactoryType;
  switch (type) {
    case Type::kNavigation:  // Subframe navigations; main frames are skipped
                             // by ClassifyRequest().
    case Type::kDocumentSubResource:
    case Type::kWorkerMainResource:
    case Type::kWorkerSubResource:
    case Type::kServiceWorkerScript:
    case Type::kServiceWorkerSubResource:
    case Type::kPrefetch:
    case Type::kEarlyHints:
      return true;
    case Type::kDownload:  // The user asked for the file.
    case Type::kDevTools:  // The user's own tool.
      return false;
  }
  return false;
}

void AdblockProxyingURLLoaderFactory::CreateLoaderAndStart(
    mojo::PendingReceiver<network::mojom::URLLoader> loader_receiver,
    int32_t request_id,
    uint32_t options,
    const network::ResourceRequest& request,
    mojo::PendingRemote<network::mojom::URLLoaderClient> client,
    const net::MutableNetworkTrafficAnnotationTag& traffic_annotation) {
  std::optional<ClassifiedRequest> classified =
      ClassifyRequest(request, request_initiator_, isolation_info_);
  if (classified.has_value()) {
    classified->session = session_;
  } else {
    target_factory_->CreateLoaderAndStart(std::move(loader_receiver),
                                          request_id, options, request,
                                          std::move(client), traffic_annotation);
    return;
  }

  if (!service_) {
    // The browser is shutting down and the Filter Engine is gone. Nothing may
    // go out unchecked, so the request is aborted.
    mojo::Remote<network::mojom::URLLoaderClient>(std::move(client))
        ->OnComplete(network::URLLoaderCompletionStatus(net::ERR_ABORTED));
    return;
  }

  // The verdict arrives later, so the request is copied. Mojo messages of the
  // loader and the client wait in their pipes until it does.
  service_->ShouldBlock(
      std::move(*classified),
      base::BindOnce(&AdblockProxyingURLLoaderFactory::OnVerdict,
                     weak_factory_.GetWeakPtr(), std::move(loader_receiver),
                     request_id, options, network::ResourceRequest(request),
                     std::move(client), traffic_annotation));
}

void AdblockProxyingURLLoaderFactory::OnVerdict(
    mojo::PendingReceiver<network::mojom::URLLoader> loader_receiver,
    int32_t request_id,
    uint32_t options,
    network::ResourceRequest request,
    mojo::PendingRemote<network::mojom::URLLoaderClient> client,
    net::MutableNetworkTrafficAnnotationTag traffic_annotation,
    bool should_block) {
  // Counts what the engine judged (the Bench reads this to tell the engine's
  // blocking from another blocker's).
  base::UmaHistogramBoolean("Aerie.Adblock.RequestBlocked", should_block);
  if (should_block) {
    if (service_) {
      service_->RecordBlocked(frame_id_, request.url);
    }
    // Dropping |loader_receiver| closes the loader pipe. The client learns why
    // from the completion status.
    mojo::Remote<network::mojom::URLLoaderClient>(std::move(client))
        ->OnComplete(
            network::URLLoaderCompletionStatus(net::ERR_BLOCKED_BY_CLIENT));
    return;
  }
  // A judged and allowed request is wrapped, so that its redirects are judged
  // too. (Requests that are never judged skip the wrapper, see
  // CreateLoaderAndStart().)
  AdblockProxyingURLLoader::CreateAndStart(
      target_factory_.get(), std::move(loader_receiver), request_id, options,
      request, std::move(client), traffic_annotation, request_initiator_,
      isolation_info_, service_, frame_id_, session_);
}

void AdblockProxyingURLLoaderFactory::OnTargetFactoryError() {
  DisconnectReceiversAndDestroy();
}

}  // namespace aerie_adblock
