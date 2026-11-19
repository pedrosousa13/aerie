// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_tab_model/arc_import_json.h"

#include <stddef.h>

#include <optional>
#include <string_view>

#include "base/json/json_reader.h"
#include "base/values.h"

namespace aerie_tab_model {

namespace {

// Arc's file nests about 10 levels (a list of containers, a list of items, an
// item, its data, its tab). The reader's own limit is 200; this is lower.
constexpr size_t kMaxJsonDepth = 64;

}  // namespace

// The one place where the import reads JSON. The issue asked for Chromium's
// data_decoder service, which parses in another process. This Chromium (157)
// has no JSON method in that service any more (docs/arc-import.md), so the
// parse is Chromium's memory-safe reader, with a depth limit, and callers run
// it on a thread-pool sequence. A later isolated parser replaces this body.
std::optional<base::Value> ParseUntrustedJson(std::string_view json) {
  return base::JSONReader::Read(json, base::JSON_REPLACE_INVALID_CHARACTERS,
                                kMaxJsonDepth);
}

}  // namespace aerie_tab_model
