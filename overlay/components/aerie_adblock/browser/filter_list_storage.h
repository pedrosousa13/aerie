// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_ADBLOCK_BROWSER_FILTER_LIST_STORAGE_H_
#define COMPONENTS_AERIE_ADBLOCK_BROWSER_FILTER_LIST_STORAGE_H_

#include <optional>
#include <string>
#include <string_view>

#include "base/files/file_path.h"
#include "components/aerie_adblock/browser/filter_list_state.h"

namespace aerie_adblock {

// Files of the filter list settings, in one directory (the browser's user data
// directory, so they are shared by every profile like the engine is):
//
//   <dir>/state.json         the FilterListState
//   <dir>/lists/<id>.txt     the downloaded copy of a list
//
// All writes go through a temp file and an atomic rename, so a crash leaves
// the old copy. These functions block; call them from a blocking sequence
// (or, at startup, with ScopedAllowBlocking).
namespace filter_list_storage {

inline constexpr char kStateFile[] = "state.json";
inline constexpr char kListsDir[] = "lists";

// A state file that is missing, unreadable or not JSON gives the defaults.
FilterListState LoadState(const base::FilePath& dir);
bool SaveState(const base::FilePath& dir, const FilterListState& state);

// The path of the downloaded copy of |id|, or an empty path if |id| is not a
// valid id (so no path is ever made from untrusted text).
base::FilePath ListPath(const base::FilePath& dir, std::string_view id);

bool WriteList(const base::FilePath& dir, std::string_view id, std::string_view text);
void DeleteList(const base::FilePath& dir, std::string_view id);

}  // namespace filter_list_storage

}  // namespace aerie_adblock

#endif  // COMPONENTS_AERIE_ADBLOCK_BROWSER_FILTER_LIST_STORAGE_H_
