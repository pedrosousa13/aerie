// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_SIDEBAR_AERIE_SIDEBAR_MOVE_H_
#define COMPONENTS_AERIE_SIDEBAR_AERIE_SIDEBAR_MOVE_H_

#include <string>
#include <vector>

#include "base/functional/function_ref.h"
#include "components/aerie_tab_model/folder_tree.h"
#include "components/aerie_tab_model/tab_model_types.h"

// Moving a tab or a folder to a new place (issue #182): what a drop writes into
// the Tab Model, and what the keys Alt+Arrow ask for. Headless.
namespace aerie_sidebar {

struct MoveRequest {
  aerie_tab_model::ItemKind kind = aerie_tab_model::ItemKind::kTab;
  std::string id;
  aerie_tab_model::Section section = aerie_tab_model::Section::kToday;
  // The folder that holds the item afterwards; empty is the section itself.
  std::string container;
  // The place among the visible children of the container, not counting the
  // item itself.
  int index = 0;
};

enum class MoveResult {
  kMoved,
  // Already there; nothing was written.
  kUnchanged,
  kRefusedTooDeep,
  kRefusedFavoritesFull,
  // Unknown item or container, a folder in Favorites, or a loop.
  kInvalid,
};

// Writes the move: the item goes to the place, the siblings are numbered
// 0..n-1 in the new order, a folder takes its tabs and folders along into the
// new section, and the pinned URL follows the section (Pinned and Favorites keep
// one, Today has none; `url_for_tab` gives the URL for a tab that gets its
// first). Items that `tab_is_listed` turns away and archived tabs keep their
// relative order; `request.index` counts the listed ones only.
MoveResult ApplyMove(aerie_tab_model::TabModel& model,
                     const MoveRequest& request,
                     base::FunctionRef<bool(const std::string&)> tab_is_listed,
                     base::FunctionRef<std::string(const std::string&)>
                         url_for_tab);

enum class MoveKey { kUp, kDown, kOutdent, kIndent };

struct KeyboardMove {
  // False: the key does nothing here (the first row, no folder before it).
  bool applies = false;
  // True: the key would nest too deeply; show the hint, change nothing.
  bool too_deep = false;
  MoveRequest request;
  // For "Moved to Trips, position 2 of 4": the folder (empty: the section),
  // the 1-based position among the siblings after the move, and their count.
  std::string announce_container;
  int announce_position = 0;
  int announce_count = 0;
};

// The key on the row at `focused` of the visible `rows` of `section`.
//   Up, Down: one place among the siblings.
//   Indent: the end of the folder just before it, if that is a folder and
//           CanNest allows it.
//   Outdent: out of its folder, just after it.
KeyboardMove ComputeKeyboardMove(const aerie_tab_model::TabModel& model,
                                 aerie_tab_model::Section section,
                                 const std::vector<aerie_tab_model::TreeRow>& rows,
                                 int focused,
                                 MoveKey key);

}  // namespace aerie_sidebar

#endif  // COMPONENTS_AERIE_SIDEBAR_AERIE_SIDEBAR_MOVE_H_
