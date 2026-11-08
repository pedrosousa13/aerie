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
// `partition` is the room for keying on a Workspace later (M5, #121): a
// non-empty value gives the same site a different key. Callers pass nothing
// today.
std::string NoiseSiteKey(const url::Origin& origin,
                         std::string_view partition = std::string_view());

// The content-setting pattern that covers exactly the hosts that share
// NoiseSiteKey's site, scheme included: "https://[*.]example.com" for a
// registrable domain, "http://localhost" (the host itself, with its scheme)
// for an IP address or a name with no registrable domain. Any port matches.
// Returns an invalid pattern for an address that is not http or https.
ContentSettingsPattern NoiseSitePattern(const GURL& url);

}  // namespace aerie_fingerprint

#endif  // COMPONENTS_AERIE_FINGERPRINT_SITE_KEY_H_
