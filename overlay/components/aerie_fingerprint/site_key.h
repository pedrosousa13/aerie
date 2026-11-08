// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_FINGERPRINT_SITE_KEY_H_
#define COMPONENTS_AERIE_FINGERPRINT_SITE_KEY_H_

#include <optional>
#include <string>
#include <string_view>

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
// `partition` is the room for keying on a Workspace later (M5, #121): a
// non-empty value gives the same site a different key. Callers pass nothing
// today.
std::string NoiseSiteKey(const url::Origin& origin,
                         std::string_view partition = std::string_view());

// The content-setting pattern that covers exactly the hosts that share
// NoiseSiteKey's site: "[*.]example.com" for a registrable domain, the host
// itself for an IP address or a name with no registrable domain ("localhost").
// The scheme is a wildcard on purpose. Returns nothing for an address that is
// not http or https.
std::optional<std::string> NoiseSitePattern(const GURL& url);

}  // namespace aerie_fingerprint

#endif  // COMPONENTS_AERIE_FINGERPRINT_SITE_KEY_H_
