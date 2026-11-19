// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_TAB_MODEL_ARC_IMPORT_JSON_H_
#define COMPONENTS_AERIE_TAB_MODEL_ARC_IMPORT_JSON_H_

#include <optional>
#include <string_view>

#include "base/values.h"

namespace aerie_tab_model {

// Reads `json` into a Value on a thread that is not the UI thread, with a depth
// limit, and returns nullopt for anything that is not JSON (arc_import_json.cc;
// docs/arc-import.md says why this is not the data_decoder service). It is the
// only place where the Arc import reads JSON, so a later isolated parser
// replaces one function.
std::optional<base::Value> ParseUntrustedJson(std::string_view json);

}  // namespace aerie_tab_model

#endif  // COMPONENTS_AERIE_TAB_MODEL_ARC_IMPORT_JSON_H_
