// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_fingerprint/site_key.h"

#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "net/base/schemeful_site.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace aerie_fingerprint {

std::string NoiseSiteKey(const url::Origin& origin,
                         std::string_view partition) {
  if (origin.opaque()) {
    return std::string();
  }
  const net::SchemefulSite site(origin);
  if (site.opaque()) {
    return std::string();
  }
  std::string key = site.Serialize();
  if (!partition.empty()) {
    key.push_back('\n');
    key.append(partition);
  }
  return key;
}

std::optional<std::string> NoiseSitePattern(const GURL& url) {
  if (!url.is_valid() || !url.SchemeIsHTTPOrHTTPS()) {
    return std::nullopt;
  }
  const std::string domain =
      net::registry_controlled_domains::GetDomainAndRegistry(
          url, net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES);
  if (!domain.empty()) {
    return "[*.]" + domain;
  }
  return std::string(url.host());
}

}  // namespace aerie_fingerprint
