// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_adblock/browser/request_classifier.h"

#include <utility>

#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/mojom/fetch_api.mojom.h"
#include "third_party/blink/public/mojom/loader/resource_load_info.mojom-shared.h"
#include "url/gurl.h"
#include "url/url_constants.h"

namespace aerie_adblock {

namespace {

// Returns false for a destination that is not judged at all.
bool KindFor(const network::ResourceRequest& request, RequestKind* kind) {
  switch (request.destination) {
    case network::mojom::RequestDestination::kDocument:
      // A main frame document. Not blocked in the first Filter Engine.
      return false;
    case network::mojom::RequestDestination::kIframe:
    case network::mojom::RequestDestination::kFrame:
    case network::mojom::RequestDestination::kFencedframe:
      *kind = RequestKind::kSubdocument;
      return true;
    case network::mojom::RequestDestination::kScript:
    case network::mojom::RequestDestination::kServiceWorker:
    case network::mojom::RequestDestination::kSharedWorker:
    case network::mojom::RequestDestination::kWorker:
    case network::mojom::RequestDestination::kAudioWorklet:
    case network::mojom::RequestDestination::kPaintWorklet:
      *kind = RequestKind::kScript;
      return true;
    case network::mojom::RequestDestination::kStyle:
    case network::mojom::RequestDestination::kXslt:
      *kind = RequestKind::kStylesheet;
      return true;
    case network::mojom::RequestDestination::kImage:
      *kind = RequestKind::kImage;
      return true;
    case network::mojom::RequestDestination::kFont:
      *kind = RequestKind::kFont;
      return true;
    case network::mojom::RequestDestination::kAudio:
    case network::mojom::RequestDestination::kVideo:
    case network::mojom::RequestDestination::kTrack:
      *kind = RequestKind::kMedia;
      return true;
    case network::mojom::RequestDestination::kEmpty:
      // fetch(), XMLHttpRequest and beacons all have no destination. Only
      // <a ping> and sendBeacon() are pings; a fetch() with keepalive is not.
      *kind = request.resource_type ==
                      static_cast<int>(blink::mojom::ResourceType::kPing)
                  ? RequestKind::kPing
                  : RequestKind::kXmlHttpRequest;
      return true;
    case network::mojom::RequestDestination::kReport:
      *kind = RequestKind::kPing;
      return true;
    case network::mojom::RequestDestination::kEmbed:
    case network::mojom::RequestDestination::kObject:
      *kind = RequestKind::kObject;
      return true;
    default:
      // manifest, web bundles, JSON modules, and any destination a later
      // Chromium adds.
      *kind = RequestKind::kOther;
      return true;
  }
}

// The top-level origin if the browser knows one, else null.
const url::Origin* TopLevelOrigin(const network::ResourceRequest& request,
                                  const net::IsolationInfo& isolation_info) {
  if (request.trusted_params.has_value()) {
    const std::optional<url::Origin>& top =
        request.trusted_params->isolation_info.top_frame_origin();
    if (top.has_value()) {
      return &*top;
    }
  }
  const std::optional<url::Origin>& top = isolation_info.top_frame_origin();
  return top.has_value() ? &*top : nullptr;
}

}  // namespace

std::optional<ClassifiedRequest> ClassifyRequest(
    const network::ResourceRequest& request,
    const url::Origin& factory_initiator,
    const net::IsolationInfo& isolation_info) {
  if (!request.url.is_valid() || !request.url.SchemeIsHTTPOrHTTPS() ||
      !request.url.has_host()) {
    return std::nullopt;
  }

  RequestKind kind = RequestKind::kOther;
  if (!KindFor(request, &kind)) {
    return std::nullopt;
  }

  const url::Origin* top = TopLevelOrigin(request, isolation_info);
  const url::Origin& first_party =
      top ? *top
          : (request.request_initiator.has_value() ? *request.request_initiator
                                                   : factory_initiator);

  ClassifiedRequest out;
  out.url = request.url.spec();
  out.hostname = request.url.host();
  out.kind = kind;

  if (first_party.opaque()) {
    // Sandboxed frames, data: documents and browser-initiated loads. No first
    // party is known, so the request counts as third party.
    out.third_party = true;
    return out;
  }
  if (first_party.scheme() != url::kHttpScheme &&
      first_party.scheme() != url::kHttpsScheme) {
    // chrome://, chrome-untrusted://, extensions, file://, and so on.
    return std::nullopt;
  }

  out.source_hostname = first_party.host();
  out.third_party = !net::registry_controlled_domains::SameDomainOrHost(
      request.url, first_party,
      net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES);
  return out;
}

}  // namespace aerie_adblock
