// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_tab_model/session_record_id.h"

namespace aerie_tab_model {

namespace {

bool IsIdCharacter(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') || c == '-' || c == '_';
}

}  // namespace

std::optional<std::string> ParseSessionRecordId(std::string_view raw) {
  if (raw.empty() || raw.size() > kMaxSessionRecordIdLength) {
    return std::nullopt;
  }
  for (char c : raw) {
    if (!IsIdCharacter(c)) {
      return std::nullopt;
    }
  }
  return std::string(raw);
}

}  // namespace aerie_tab_model
