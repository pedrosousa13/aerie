// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_ADBLOCK_BROWSER_REQUEST_CLASSIFIER_H_
#define COMPONENTS_AERIE_ADBLOCK_BROWSER_REQUEST_CLASSIFIER_H_

#include <optional>

#include "components/aerie_adblock/browser/classified_request.h"
#include "net/base/isolation_info.h"
#include "url/origin.h"

namespace network {
struct ResourceRequest;
}  // namespace network

namespace aerie_adblock {

// Decides whether a request is one the Filter Engine judges at all, and if so
// describes it. Returns nullopt when the request must pass untouched:
//   - the URL is not http or https (data:, blob:, chrome:, file:, ...);
//   - the request loads a main frame document (main-frame navigation blocking
//     is out of scope);
//   - the first party is a browser page or an extension (not http or https),
//     because Aerie filters web content, not its own UI or the user's
//     extensions.
//
// First party context: the top-level site, as the browser process knows it. In
// order: the request's own trusted IsolationInfo (navigations carry one), then
// |isolation_info| (the factory's, set for factories that serve a document or
// worker), and only then the initiator. So an ad iframe's own scripts and
// pixels are third party to the page, and a browser-initiated subframe
// navigation (reload, back, restore), which has no initiator, still knows the
// page it belongs to. |source_hostname| is that top-level host and
// |third_party| compares the request to it.
//
// |factory_initiator| is the initiator the factory was created for, used when
// no top-level origin is known.
std::optional<ClassifiedRequest> ClassifyRequest(
    const network::ResourceRequest& request,
    const url::Origin& factory_initiator,
    const net::IsolationInfo& isolation_info);

}  // namespace aerie_adblock

#endif  // COMPONENTS_AERIE_ADBLOCK_BROWSER_REQUEST_CLASSIFIER_H_
