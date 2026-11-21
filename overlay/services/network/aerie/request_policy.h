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
// What is judged, and what is not, is in docs/network-endpoints.md ("Request
// policy", "Limits"). It is not every request: renderer traffic, raw DNS,
// QUIC alternative services and sockets that are not URL requests are outside.
//
// The tables are in request_allowlist.inc, generated from the same page
// (./aerie netpolicy generate).
namespace aerie_network {

// Where a request comes from.
enum class RequestOrigin {
  // Not known. Treated as the browser's own: the policy fails closed. This is
  // the default, so a request that nobody described is never taken for a
  // page's.
  kUnknown,
  // The browser's own, or any factory that is not positively a renderer's.
  kBrowserProcess,
  // A factory positively known to serve a renderer (see ClassifyFactory).
  // Never judged.
  kRenderer,
};

// What the network service knows about the URLLoaderFactory that carries a
// request. These come from the factory's parameters, set by the browser, and
// never from anything in the request that a page can set.
struct FactoryFacts {
  bool is_browser_process = true;
  // The process id names a real renderer process (not the browser, not null).
  bool is_valid_renderer_process = false;
  // The factory is for the exclusive use of a trusted consumer.
  bool is_trusted = true;
  // The factory locks the request initiator to an origin. Chromium requires
  // this on every factory it vends to a renderer.
  bool has_initiator_origin_lock = false;
};

// A factory is a renderer's only when all of these hold: a real renderer
// process, not trusted, and locked to an initiator origin. A request with no
// URLLoader at all (|factory| is empty) is kUnknown. Every other case,
// including a utility process, is kBrowserProcess, and is judged.
RequestOrigin ClassifyFactory(const std::optional<FactoryFacts>& factory);

struct RequestFacts {
  RequestOrigin origin = RequestOrigin::kUnknown;
  // The unique id hash of the traffic annotation. Empty, or the hash of
  // "missing", "undefined" or the uninitialised value, counts as no
  // annotation.
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

// Decides one URL request. Pure: it reads the facts and the compiled-in
// tables and nothing else, so no pref, header or extension can widen the
// allowlist.
//
// * A renderer's request is allowed (it is a page's or an extension's).
// * Only http, https, ws and wss are judged; the other schemes reach no host.
// * Otherwise the request is the browser's own. No usable annotation:
//   refused. An annotation the user or a site directs: allowed for any host.
//   An annotation with a row for the host (or a parent domain): allowed.
//   Anything else: refused.
Verdict Decide(const RequestFacts& facts);

// Decides a preconnect or a name lookup that the browser asks the network
// context for. With a usable annotation it is Decide(). Without one (a lookup
// has none), the host decides: allowed only if some row names it.
Verdict DecideConnection(const RequestFacts& facts);

bool IsRefusal(Verdict verdict);

// Name of the histogram that counts refusals, by Verdict. The Bench reads it.
inline constexpr char kRefusedHistogram[] =
    "Aerie.Network.RequestPolicy.Refused";

// Counts a refusal in kRefusedHistogram and logs it (RefusalLogMessage).
void RecordRefusal(Verdict verdict, const GURL& url);

// One line for the log: the reason and the host. Nothing else of the request
// is taken as input, so the scheme, port, credentials, path, query, fragment
// and headers (cookies among them) cannot appear in it.
std::string RefusalLogMessage(Verdict verdict, const GURL& url);

}  // namespace aerie_network

#endif  // SERVICES_NETWORK_AERIE_REQUEST_POLICY_H_
