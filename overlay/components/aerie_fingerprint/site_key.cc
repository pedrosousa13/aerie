// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_fingerprint/site_key.h"

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

std::string NoisePartitionKey(std::string_view partition_domain,
                              std::string_view partition_name) {
  if (partition_domain.empty()) {
    return std::string();
  }
  std::string key(partition_domain);
  key.push_back('/');
  key.append(partition_name);
  return key;
}

ContentSettingsPattern NoiseSitePattern(const GURL& url) {
  if (!url.is_valid() || !url.SchemeIsHTTPOrHTTPS()) {
    return ContentSettingsPattern();
  }
  // The same private-registry rules as net::SchemefulSite, so the pattern and
  // the key cannot disagree: scheme, then the registrable domain with a domain
  // wildcard (the host itself when there is none), any port.
  return ContentSettingsPattern::FromURLToSchemefulSitePattern(url);
}

}  // namespace aerie_fingerprint
