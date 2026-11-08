// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_ADBLOCK_BROWSER_ADBLOCK_ENGINE_H_
#define COMPONENTS_AERIE_ADBLOCK_BROWSER_ADBLOCK_ENGINE_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "base/containers/span.h"
#include "base/time/time.h"
#include "components/aerie_adblock/browser/classified_request.h"
#include "components/aerie_adblock/browser/list_failure.h"
#include "components/aerie_adblock/rust/ffi/src/lib.rs.h"
#include "third_party/rust/cxx/v1/cxx.h"

namespace aerie_adblock {

// What a good list contained (see CheckList()).
struct ListCheck {
  ListFailure failure = ListFailure::kNone;
  int rules = 0;       // Network rules, when |failure| is kNone.
  // Exception rules that match every address. The engine ignores them; the
  // caller should log a warning.
  int ignored_exceptions = 0;
  std::string title;   // The list's own "! Title:", cut short; may be empty.
};

// Decides whether |text| is a usable filter list: text, not an HTML page, at
// least one network rule, under the size limit, parsed within |budget|. Builds
// nothing and keeps nothing; safe on any thread. See aerie_adblock_core.
ListCheck CheckList(base::span<const uint8_t> text, base::TimeDelta budget);

struct BuildResult {
  ListFailure failure = ListFailure::kNone;
  size_t failed_index = 0;  // Which list failed, when |failure| is not kNone.
  // Exception rules, over all the lists, that match every address and were
  // ignored. Set when |failure| is kNone.
  size_t ignored_exceptions = 0;
};

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

  // Checks every list in |lists| (they share |budget|) and, only if all are
  // good, replaces the rules with all of them compiled together. On a failure
  // the old rules stay and the result names the list.
  BuildResult LoadLists(const std::vector<std::string>& lists,
                        base::TimeDelta budget);

  // Replaces the rules with one filter list in EasyList syntax. For tests.
  void LoadListForTesting(std::string_view list);

  // The rules in the form LoadSerialized() reads. For tests.
  std::vector<uint8_t> SerializeForTesting() const;

  // True if the request must be blocked. Requests with non-ASCII strings
  // (never produced by ClassifyRequest) are not blocked.
  bool ShouldBlock(const ClassifiedRequest& request) const;

  // The CSS to inject when the page at |page_url| (a GURL spec) commits: one
  // `selector{display:none!important}` line per hide rule for the site. Every
  // selector passed the Rust core's safety check. Empty for a URL that is not
  // ASCII http(s) (GURL specs are always ASCII).
  std::string CosmeticStylesheet(std::string_view page_url) const;

  // The CSS for generic hide rules that match the class names and ids a page
  // uses (|classes| and |ids|: whitespace separated). Empty when the site
  // has a $generichide exception or an argument is not ASCII.
  std::string GenericStylesheet(std::string_view page_url,
                                std::string_view classes,
                                std::string_view ids) const;

 private:
  rust::Box<Engine> engine_;
};

}  // namespace aerie_adblock

#endif  // COMPONENTS_AERIE_ADBLOCK_BROWSER_ADBLOCK_ENGINE_H_
