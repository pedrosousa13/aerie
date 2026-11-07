// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_ADBLOCK_BROWSER_ADBLOCK_ENGINE_H_
#define COMPONENTS_AERIE_ADBLOCK_BROWSER_ADBLOCK_ENGINE_H_

#include <cstdint>
#include <string_view>
#include <vector>

#include "base/containers/span.h"
#include "components/aerie_adblock/browser/classified_request.h"
#include "components/aerie_adblock/rust/ffi/src/lib.rs.h"
#include "third_party/rust/cxx/v1/cxx.h"

namespace aerie_adblock {

// C++ face of the Rust engine (adblock-rust, used unmodified). Not
// thread-safe: use it on one sequence only. FilterEngineService owns the
// browser's single instance and keeps it on its own sequence.
class AdblockEngine {
 public:
  // Starts with no rules; it blocks nothing until a list is loaded.
  AdblockEngine();
  ~AdblockEngine();

  AdblockEngine(const AdblockEngine&) = delete;
  AdblockEngine& operator=(const AdblockEngine&) = delete;

  // Replaces the rules with a serialized engine (what the build tool
  // aerie_adblock_compile writes). Returns false, and keeps the old rules,
  // when |data| is not a valid engine.
  bool LoadSerialized(base::span<const uint8_t> data);

  // Replaces the rules with one filter list in EasyList syntax. For tests.
  void LoadListForTesting(std::string_view list);

  // The rules in the form LoadSerialized() reads. For tests.
  std::vector<uint8_t> SerializeForTesting() const;

  // True if the request must be blocked. Requests with non-ASCII strings
  // (never produced by ClassifyRequest) are not blocked.
  bool ShouldBlock(const ClassifiedRequest& request) const;

 private:
  rust::Box<Engine> engine_;
};

}  // namespace aerie_adblock

#endif  // COMPONENTS_AERIE_ADBLOCK_BROWSER_ADBLOCK_ENGINE_H_
