// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_adblock/browser/list_title.h"

#include <algorithm>

#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversion_utils.h"

namespace aerie_adblock {

namespace {

constexpr std::string_view kTitleKey = "title:";

bool IsControl(char c) {
  unsigned char u = static_cast<unsigned char>(c);
  return u < 0x20 || u == 0x7f;
}

// The title text of one header line, or nullopt-like false when the line is
// not a title.
bool TitleOfLine(std::string_view line, std::string* out) {
  if (!line.starts_with('!')) {
    return false;
  }
  line.remove_prefix(1);
  line = base::TrimWhitespaceASCII(line, base::TRIM_LEADING);
  if (line.size() < kTitleKey.size() ||
      !base::EqualsCaseInsensitiveASCII(line.substr(0, kTitleKey.size()),
                                        kTitleKey)) {
    return false;
  }
  line.remove_prefix(kTitleKey.size());
  out->clear();
  for (char c : line) {
    if (!IsControl(c)) {
      out->push_back(c);
    }
  }
  return true;
}

}  // namespace

std::string ParseListTitle(std::string_view text) {
  text = text.substr(0, kMaxListHeaderBytes);
  size_t lines = 0;
  while (!text.empty() && lines < kMaxListHeaderLines) {
    size_t end = text.find('\n');
    std::string_view line = text.substr(0, end);
    text = end == std::string_view::npos ? std::string_view()
                                         : text.substr(end + 1);
    ++lines;
    std::string_view trimmed = base::TrimWhitespaceASCII(line, base::TRIM_ALL);
    std::string title;
    if (TitleOfLine(trimmed, &title)) {
      std::string_view value =
          base::TrimWhitespaceASCII(title, base::TRIM_ALL);
      size_t size = std::min(value.size(), kMaxListTitleBytes);
      // Never end inside a UTF-8 sequence: back up to a character boundary.
      while (size > 0 && size < value.size() &&
             (static_cast<unsigned char>(value[size]) & 0xC0) == 0x80) {
        --size;
      }
      std::string result(value.substr(0, size));
      // Drop a text that is not valid UTF-8 rather than show a broken one.
      return base::IsStringUTF8(result) ? result : std::string();
    }
    // The header is the comment lines ("!", "[Adblock Plus 2.0]") and blank
    // lines at the top. A rule ends it.
    if (!trimmed.empty() && !trimmed.starts_with('!') &&
        !trimmed.starts_with('[')) {
      break;
    }
  }
  return std::string();
}

}  // namespace aerie_adblock
