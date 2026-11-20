// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef SERVICES_NETWORK_AERIE_REQUEST_POLICY_H_
#define SERVICES_NETWORK_AERIE_REQUEST_POLICY_H_

#include <cstdint>
#include <optional>
#include <string>

#include "url/gurl.h"

// The request policy (issue #257): the network service refuses a request that
// the browser makes on its own unless it is on a reviewed allowlist. A request
// that a later Chromium or Helium adds is refused at run time, not only caught
// by the Bench afterwards.
//
// The design is Crest's (https://github.com/pauljoda/Crest,
// services/network/crest_request_policy.cc, MPL-2.0): decide in the network
// service from where the request comes from and its traffic annotation. This
// code is written for Aerie and shares none of Crest's source. Unlike Crest's,
// it judges every host, not only Google's.
//
// The tables are in request_allowlist.inc, generated from
// docs/network-endpoints.md (./aerie netpolicy generate).
namespace aerie_network {

// Where a request comes from, as the network service knows it from the
// URLLoaderFactory that carries it (never from anything in the request that a
// page can set).
enum class RequestOrigin {
  // Not known. Treated as the browser's own: the policy fails closed. This is
  // the default, so a request that nobody described is never taken for a page's.
  kUnknown,
  // A factory of the browser process: the browser's own requests, and also the
  // navigations and downloads the user starts (their annotations say so).
  kBrowserProcess,
  // A renderer process: a page, a worker or an extension. Never judged.
  kRenderer,
};

struct RequestFacts {
  RequestOrigin origin = RequestOrigin::kUnknown;
  // The unique id hash of the request's traffic annotation. Empty, or the hash
  // of "missing", "undefined" or the uninitialised value, counts as no annotation.
  std::optional<int32_t> annotation_hash;
  GURL url;
};

// The values are recorded in a histogram: never renumber, only append.
enum class Verdict {
  kAllow = 0,
  kRefusedUnlistedHost = 1,
  kRefusedNoAnnotation = 2,
  kMaxValue = kRefusedNoAnnotation,
};

// Decides one request. Pure: it reads the facts and the compiled-in tables and
// nothing else, so no pref, header or extension can widen the allowlist.
//
// * A renderer's request is allowed (it is a page's or an extension's).
// * Only http, https, ws and wss are judged; the other schemes reach no host.
// * Otherwise the request is the browser's own. No usable annotation: refused.
//   An annotation that a page or the user directs: allowed for any host.
//   An annotation with a row for the host (or a parent domain): allowed.
//   Anything else: refused.
Verdict Decide(const RequestFacts& facts);

bool IsRefusal(Verdict verdict);

// Name of the histogram that counts refusals, by Verdict. The Bench reads it.
inline constexpr char kRefusedHistogram[] = "Aerie.Network.RequestPolicy.Refused";

// Counts a refusal in kRefusedHistogram and logs it (RefusalLogMessage).
void RecordRefusal(Verdict verdict, const GURL& url);

// One line for the log: the reason and the host. Never the scheme, port,
// credentials, path, query or fragment, and the caller has no cookies to pass.
std::string RefusalLogMessage(Verdict verdict, const GURL& url);

}  // namespace aerie_network

#endif  // SERVICES_NETWORK_AERIE_REQUEST_POLICY_H_
