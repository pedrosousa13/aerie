// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_ADBLOCK_BROWSER_PROTECTION_ENGINE_H_
#define COMPONENTS_AERIE_ADBLOCK_BROWSER_PROTECTION_ENGINE_H_

#include <cstdint>
#include <string_view>
#include <vector>

#include "base/containers/span.h"
#include "components/aerie_adblock/rust/ffi/src/safety.rs.h"
#include "third_party/rust/cxx/v1/cxx.h"

namespace aerie_adblock {

// C++ face of the Rust protection engine: the set of phishing and malware
// domains (the protection category of the Filter Engine). Not thread-safe: use
// it on one sequence only. ProtectionService owns the browser's instance.
class ProtectionEngine {
 public:
  // Starts with no domains; it lists nothing until a list is loaded.
  ProtectionEngine();
  ~ProtectionEngine();

  ProtectionEngine(const ProtectionEngine&) = delete;
  ProtectionEngine& operator=(const ProtectionEngine&) = delete;

  // Replaces the domains with a serialized engine (what the build tool
  // aerie_protection_compile writes). Returns false, and keeps the old
  // domains, when |data| is not valid.
  bool LoadSerialized(base::span<const uint8_t> data);

  // Replaces the domains with one list (domain per line, or hosts format).
  // For tests, and for the Bench's test list (see ProtectionService).
  void LoadListForTesting(std::string_view list);

  // The domains in the form LoadSerialized() reads. For tests.
  std::vector<uint8_t> SerializeForTesting() const;

  // True if |host| (as GURL::host() gives it) is a listed domain or a
  // subdomain of one. A host with non-ASCII bytes (never produced by GURL) is
  // not listed.
  bool IsListed(std::string_view host) const;

  size_t domain_count() const;

 private:
  rust::Box<ProtectionDomains> engine_;
};

}  // namespace aerie_adblock

#endif  // COMPONENTS_AERIE_ADBLOCK_BROWSER_PROTECTION_ENGINE_H_
