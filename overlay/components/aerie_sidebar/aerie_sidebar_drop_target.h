// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_SIDEBAR_AERIE_SIDEBAR_DROP_TARGET_H_
#define COMPONENTS_AERIE_SIDEBAR_AERIE_SIDEBAR_DROP_TARGET_H_

#include <string>
#include <vector>

#include "components/aerie_tab_model/folder_tree.h"
#include "components/aerie_tab_model/tab_model_types.h"

// Where a dragged tab or folder would land (issue #182, docs/sidebar-design.md,
// "Drag rules over a folder row"). Headless: the view measures its rows and
// tiles once per move and asks. Coordinates are the sidebar view's, y grows
// downward.
namespace aerie_sidebar {

enum class DropKind {
  // The pointer is over nothing that takes a drop.
  kNone,
  // Next to a row, as a sibling of it.
  kBefore,
  kAfter,
  // Into the folder under the pointer, at the end of it.
  kInto,
  // The bottom edge of an expanded folder: the first child.
  kFirstChild,
  // A slot of the Favorites grid.
  kFavoritesSlot,
  // A drop that would be wrong; nothing moves.
  kRefused,
};

enum class RefusalReason {
  kNone,
  // More than kMaxFolderDepth levels: the hint "Can't nest deeper than 5
  // levels" shows.
  kTooDeep,
  // A folder into itself or into one of its own descendants.
  kIntoItself,
  // The 13th Favorite: the same toast as "Add to Favorites".
  kFavoritesFull,
  // Folders do not go into Favorites.
  kFolderInFavorites,
};

struct DropRow {
  aerie_tab_model::TreeRow row;
  int top = 0;
  int height = 0;
};

// One list (Pinned or Today): the area that takes drops, and its visible rows
// in order. A scrolled-away row is left out by the caller.
struct DropList {
  aerie_tab_model::Section section = aerie_tab_model::Section::kToday;
  int top = 0;
  int bottom = 0;
  std::vector<DropRow> rows;
};

struct DropRect {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
};

struct DropGeometry {
  // Pinned and Today; a list with `bottom <= top` takes no drop.
  DropList pinned;
  DropList today;
  // The Favorites grid: its area, and the tiles in order, including the
  // dragged one. An area with no height takes no drop.
  DropRect favorites_area;
  std::vector<DropRect> tiles;
  // Record ids of `tiles`, same order.
  std::vector<std::string> tile_ids;
  // The hairlines between Favorites and Pinned, and between Pinned and Today,
  // with the hit area around each (about 8 px). They are always there, even
  // when the section above is empty. A drop on the Favorites divider is a drop
  // at the end of Favorites; on the Pinned divider, at the end of Pinned. A
  // divider with no height takes no drop.
  DropRect favorites_divider;
  DropRect pinned_divider;
};

struct DragItem {
  aerie_tab_model::ItemKind kind = aerie_tab_model::ItemKind::kTab;
  std::string id;
};

struct DropTarget {
  DropKind kind = DropKind::kNone;
  aerie_tab_model::Section section = aerie_tab_model::Section::kToday;
  // The folder that will hold the item; empty is the section itself.
  std::string container;
  // The place among the container's children once the item is out of its old
  // place (so the item itself is not counted).
  int index = 0;
  // The row the indicator belongs to (before, after, into, first child); empty
  // for an empty list or a Favorites slot.
  std::string anchor_id;
  RefusalReason reason = RefusalReason::kNone;
  // True when the pointer is over a divider (also for a refusal there), so the
  // view highlights that divider.
  bool on_divider = false;
};

// How many Favorites the Workspace has, and whether `item` is one already, is
// read from `model`.
DropTarget ComputeDropTarget(const aerie_tab_model::TabModel& model,
                             int x,
                             int y,
                             const DropGeometry& geometry,
                             const DragItem& item);

}  // namespace aerie_sidebar

#endif  // COMPONENTS_AERIE_SIDEBAR_AERIE_SIDEBAR_DROP_TARGET_H_
