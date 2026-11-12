// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_TAB_MODEL_SESSION_RECORD_ID_H_
#define COMPONENTS_AERIE_TAB_MODEL_SESSION_RECORD_ID_H_

#include <optional>
#include <string>
#include <string_view>

namespace aerie_tab_model {

// The longest id that is read back. Record ids are UUIDs (36 characters).
inline constexpr size_t kMaxSessionRecordIdLength = 64;

// Checks a record id that was read back from the session file. The file is
// untrusted input from disk, so a value is an id only when it is 1 to
// kMaxSessionRecordIdLength characters of A-Z, a-z, 0-9, '-' and '_'. Anything
// else (empty, too long, other characters, control characters) is "no id".
std::optional<std::string> ParseSessionRecordId(std::string_view raw);

}  // namespace aerie_tab_model

#endif  // COMPONENTS_AERIE_TAB_MODEL_SESSION_RECORD_ID_H_
