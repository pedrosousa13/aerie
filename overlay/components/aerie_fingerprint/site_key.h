// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_FINGERPRINT_SITE_KEY_H_
#define COMPONENTS_AERIE_FINGERPRINT_SITE_KEY_H_

#include <string>
#include <string_view>

#include "components/content_settings/core/common/content_settings_pattern.h"

class GURL;

namespace url {
class Origin;
}  // namespace url

namespace aerie_fingerprint {

// The string the per-site fingerprinting tokens are derived from. It is the
// site of `origin` (scheme plus registrable domain, as net::SchemefulSite
// computes it), so every host of one site gets the same key and two sites
// never do. Returns an empty string for an opaque origin; the caller must then
// use a value that nobody can repeat.
//
// `partition` is the Workspace (#121, ADR 0006): a non-empty value, from
// NoisePartitionKey, gives the same site a different key, so two Workspaces
// get two seeds for one site. An empty value is the default partition and
// keeps the key it had before Workspaces. The key is only hashed into a token
// in memory; nothing here is stored or sent.
std::string NoiseSiteKey(const url::Origin& origin,
                         std::string_view partition = std::string_view());

// The partition part of the key, from a storage partition's domain and name
// (content::StoragePartitionConfig). The default partition has an empty
// domain and gives an empty string, so a profile without Workspaces keeps the
// keys it had. A Workspace has the domain "aerieworkspace" and its id as the
// name. A "/" joins them; a domain is [a-z]*, so it never contains one.
std::string NoisePartitionKey(std::string_view partition_domain,
                              std::string_view partition_name);

// The content-setting pattern that covers exactly the hosts that share
// NoiseSiteKey's site, scheme included: "https://[*.]example.com" for a
// registrable domain, "http://localhost" (the host itself, with its scheme)
// for an IP address or a name with no registrable domain. Any port matches.
// Returns an invalid pattern for an address that is not http or https.
ContentSettingsPattern NoiseSitePattern(const GURL& url);

}  // namespace aerie_fingerprint

#endif  // COMPONENTS_AERIE_FINGERPRINT_SITE_KEY_H_
