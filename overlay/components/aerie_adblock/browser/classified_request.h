// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_ADBLOCK_BROWSER_CLASSIFIED_REQUEST_H_
#define COMPONENTS_AERIE_ADBLOCK_BROWSER_CLASSIFIED_REQUEST_H_

#include <cstdint>
#include <string>

namespace aerie_adblock {

// What kind of resource a request fetches, in the filter list vocabulary.
// Mirrors aerie_adblock::RequestType in the Rust bridge; adblock_engine.cc maps one
// to the other, so this header does not need the generated Rust header.
enum class RequestKind : uint8_t {
  kOther,
  kDocument,
  kSubdocument,
  kScript,
  kStylesheet,
  kImage,
  kFont,
  kMedia,
  kXmlHttpRequest,
  kPing,
  kWebsocket,
  kObject,
};

// Everything the Filter Engine needs to judge one request. All strings are
// ASCII: GURL specs and hosts always are.
struct ClassifiedRequest {
  std::string url;              // GURL::spec() of the request.
  std::string hostname;         // Host of |url|.
  std::string source_hostname;  // Host of the initiator; empty if opaque.
  RequestKind kind = RequestKind::kOther;
  bool third_party = true;
};

}  // namespace aerie_adblock

#endif  // COMPONENTS_AERIE_ADBLOCK_BROWSER_CLASSIFIED_REQUEST_H_
