// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "services/network/aerie/request_policy.h"

#include <algorithm>
#include <string>
#include <string_view>

#include "base/logging.h"
#include "base/metrics/histogram_functions.h"
#include "net/traffic_annotation/network_traffic_annotation.h"

namespace aerie_network {

namespace {

using Annotation = net::internal::StringLiteralToHash;

// A browser request with this traffic annotation may reach this host or a
// subdomain of it.
struct AllowedHost {
  Annotation annotation;
  std::string_view host;
};

// An annotation whose destination is set by the user or by a site.
struct DirectedAnnotation {
  Annotation annotation;
};

// kAllowedHosts, kUserDirectedAnnotations and kSiteDirectedAnnotations,
// generated from docs/network-endpoints.md. Do not add a row here by hand.
#include "services/network/aerie/request_allowlist.inc"

constexpr int32_t kMissingHash =
    net::internal::ComputeAnnotationHash("missing");

bool HasUsableAnnotation(const std::optional<int32_t>& hash) {
  return hash && *hash != kMissingHash &&
         *hash != net::internal::UNDEFINED_HASH &&
         *hash != net::internal::TRAFFIC_ANNOTATION_UNINITIALIZED;
}

template <size_t N>
bool IsIn(const DirectedAnnotation (&list)[N], int32_t hash) {
  return std::ranges::any_of(list, [hash](const DirectedAnnotation& d) {
    return d.annotation.hash_value == hash;
  });
}

// |host| is |allowed| or a subdomain of it, on label boundaries. A trailing
// dot (the same name in DNS) is ignored.
bool HostMatches(std::string_view host, std::string_view allowed) {
  if (host.ends_with('.')) {
    host.remove_suffix(1);
  }
  if (host == allowed) {
    return true;
  }
  return host.size() > allowed.size() && host.ends_with(allowed) &&
         host[host.size() - allowed.size() - 1] == '.';
}

bool IsListedFor(int32_t hash, std::string_view host) {
  return std::ranges::any_of(kAllowedHosts, [&](const AllowedHost& row) {
    return row.annotation.hash_value == hash && HostMatches(host, row.host);
  });
}

bool IsListedForAnyAnnotation(std::string_view host) {
  return std::ranges::any_of(kAllowedHosts, [&](const AllowedHost& row) {
    return HostMatches(host, row.host);
  });
}

bool IsJudgedScheme(const GURL& url) {
  return url.SchemeIsHTTPOrHTTPS() || url.SchemeIsWSOrWSS();
}

const char* Reason(Verdict verdict) {
  switch (verdict) {
    case Verdict::kAllow:
      return "allowed";
    case Verdict::kRefusedUnlistedHost:
      return "host not on the allowlist";
    case Verdict::kRefusedNoAnnotation:
      return "no traffic annotation";
  }
  return "unknown";
}

}  // namespace

RequestOrigin ClassifyFactory(const std::optional<FactoryFacts>& factory) {
  if (!factory) {
    return RequestOrigin::kUnknown;
  }
  if (!factory->is_browser_process && factory->is_valid_renderer_process &&
      !factory->is_trusted && factory->has_initiator_origin_lock) {
    return RequestOrigin::kRenderer;
  }
  return RequestOrigin::kBrowserProcess;
}

Verdict Decide(const RequestFacts& facts) {
  if (facts.origin == RequestOrigin::kRenderer) {
    return Verdict::kAllow;
  }
  const GURL& url = facts.url;
  if (!IsJudgedScheme(url)) {
    return Verdict::kAllow;
  }
  if (!HasUsableAnnotation(facts.annotation_hash)) {
    return Verdict::kRefusedNoAnnotation;
  }
  const int32_t hash = *facts.annotation_hash;
  if (IsIn(kUserDirectedAnnotations, hash) ||
      IsIn(kSiteDirectedAnnotations, hash) || IsListedFor(hash, url.host())) {
    return Verdict::kAllow;
  }
  return Verdict::kRefusedUnlistedHost;
}

Verdict DecideConnection(const RequestFacts& facts) {
  if (facts.origin == RequestOrigin::kRenderer) {
    return Verdict::kAllow;
  }
  if (!IsJudgedScheme(facts.url)) {
    return Verdict::kAllow;
  }
  if (HasUsableAnnotation(facts.annotation_hash)) {
    return Decide(facts);
  }
  return IsListedForAnyAnnotation(facts.url.host())
             ? Verdict::kAllow
             : Verdict::kRefusedUnlistedHost;
}

bool IsRefusal(Verdict verdict) {
  return verdict != Verdict::kAllow;
}

void RecordRefusal(Verdict verdict, const GURL& url) {
  base::UmaHistogramEnumeration(kRefusedHistogram, verdict);
  LOG(WARNING) << RefusalLogMessage(verdict, url);
}

std::string RefusalLogMessage(Verdict verdict, const GURL& url) {
  std::string line = "Aerie refused a browser request to host ";
  line += url.host();
  line += " (";
  line += Reason(verdict);
  line += ")";
  return line;
}

}  // namespace aerie_network
