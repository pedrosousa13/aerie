// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_TAB_MODEL_TITLE_TIDIER_H_
#define COMPONENTS_AERIE_TAB_MODEL_TITLE_TIDIER_H_

#include <string>
#include <string_view>

// The built-in tidy of tab titles (issue #146, docs/sidebar-design.md, "Tab
// rename and title precedence"). Pure functions: they read a title and a host
// and nothing else, never the page, and they use no regular expression and no
// ICU. Their cost is linear in the title, so a title is tidied on every change
// without a cheaper path.
namespace aerie_tab_model {

// The most that a title is read to. A longer title is cut before it is
// tidied, so the cost per title has a bound whatever the page sends.
inline constexpr size_t kMaxTidyTitleBytes = 2048;

// `title` cut to kMaxTidyTitleBytes, never inside a UTF-8 sequence. The tidy
// and the rename rules read no more than this of a title.
std::string_view BoundTitle(std::string_view title);

// Tidies `title` for a page on `host` (a GURL host; may be empty):
//  - strips a leading or trailing unread count such as "(3)", "[12]" or
//    "(99+)";
//  - strips a trailing site name after " - ", " | ", " — " or " · " when the
//    name is the host's name ("... - Gmail" on mail.google.com, "... | GitHub"
//    on github.com);
//  - collapses runs of spaces and repeated or dangling separators.
// Returns the title unchanged when nothing would remain.
std::string TidyTitle(std::string_view title, std::string_view host);

// Only the third step: runs of spaces become one, repeated separators become
// one, and a separator at either end goes. The rename rules use it to close the
// gap that a removed text leaves.
std::string CleanSeparators(std::string_view text);

// True when `name` is a name for `host`: "GitHub" for github.com, "Gmail" for
// mail.google.com, "BBC News" for www.bbc.co.uk. Not case sensitive.
bool NameMatchesHost(std::string_view name, std::string_view host);

}  // namespace aerie_tab_model

#endif  // COMPONENTS_AERIE_TAB_MODEL_TITLE_TIDIER_H_
