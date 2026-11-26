// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_ADBLOCK_BROWSER_LIST_TITLE_H_
#define COMPONENTS_AERIE_ADBLOCK_BROWSER_LIST_TITLE_H_

#include <cstddef>
#include <string>
#include <string_view>

namespace aerie_adblock {

// The longest title kept, in bytes.
inline constexpr size_t kMaxListTitleBytes = 100;
// Only the header of a list is read: at most this many lines and bytes, and
// never past the first rule line.
inline constexpr size_t kMaxListHeaderLines = 50;
inline constexpr size_t kMaxListHeaderBytes = 4096;

// The value of the "! Title:" line in the header of a saved list (issue #108),
// for the Settings page to show. |text| is untrusted. The result is at most
// kMaxListTitleBytes of valid UTF-8, with control characters removed and white
// space trimmed. It is "" when the header has no title, and the page then
// shows the list's address. The text is not escaped: the page shows it as
// text and never as markup.
std::string ParseListTitle(std::string_view text);

}  // namespace aerie_adblock

#endif  // COMPONENTS_AERIE_ADBLOCK_BROWSER_LIST_TITLE_H_
