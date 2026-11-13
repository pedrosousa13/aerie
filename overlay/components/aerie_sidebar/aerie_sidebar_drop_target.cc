// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_sidebar/aerie_sidebar_drop_target.h"

#include <algorithm>
#include <cstdlib>
#include <limits>

#include "components/aerie_design_tokens/aerie_tokens.h"
#include "components/aerie_tab_model/tab_model.h"

namespace aerie_sidebar {

namespace {

using aerie_tab_model::ItemKind;
using aerie_tab_model::Section;
using aerie_tab_model::TreeRow;

DropTarget Refused(Section section, RefusalReason reason,
                   const std::string& anchor) {
  DropTarget target;
  target.kind = DropKind::kRefused;
  target.section = section;
  target.reason = reason;
  target.anchor_id = anchor;
  return target;
}

// The place of `row` among its visible siblings, not counting `dragged`.
int SiblingIndex(const DropList& list, size_t row_index,
                 const std::string& dragged) {
  const TreeRow& row = list.rows[row_index].row;
  int index = 0;
  for (size_t i = 0; i < row_index; ++i) {
    const TreeRow& other = list.rows[i].row;
    if (other.parent == row.parent && other.id != dragged) {
      ++index;
    }
  }
  return index;
}

int SiblingCount(const DropList& list, const std::string& parent,
                 const std::string& dragged) {
  int count = 0;
  for (const DropRow& r : list.rows) {
    if (r.row.parent == parent && r.row.id != dragged) {
      ++count;
    }
  }
  return count;
}

// Validates a placement in `container` with CanNest.
DropTarget Place(const aerie_tab_model::TabModel& model, DropKind kind,
                 Section section, const std::string& container, int index,
                 const std::string& anchor, const DragItem& item) {
  if (!aerie_tab_model::CanNest(model, item.kind, item.id, container)) {
    // A folder into itself or below itself is a loop; anything else that
    // fails is the depth limit.
    bool loop = false;
    if (item.kind == ItemKind::kFolder) {
      const aerie_tab_model::FolderRecord* walk = model.GetFolder(container);
      for (int i = 0; walk && i <= aerie_tab_model::kWalkBound; ++i) {
        if (walk->id == item.id) {
          loop = true;
          break;
        }
        walk = walk->parent.empty() ? nullptr : model.GetFolder(walk->parent);
      }
    }
    return Refused(section,
                   loop ? RefusalReason::kIntoItself : RefusalReason::kTooDeep,
                   anchor);
  }
  DropTarget target;
  target.kind = kind;
  target.section = section;
  target.container = container;
  target.index = index;
  target.anchor_id = anchor;
  return target;
}

DropTarget TargetInList(const aerie_tab_model::TabModel& model,
                        const DropList& list, int y, const DragItem& item) {
  const Section section = list.section;
  if (list.rows.empty()) {
    return Place(model, DropKind::kBefore, section, std::string(), 0,
                 std::string(), item);
  }
  // Above the first row: before it. Below the last: the end of the section.
  if (y < list.rows.front().top) {
    const TreeRow& first = list.rows.front().row;
    return Place(model, DropKind::kBefore, section, first.parent,
                 SiblingIndex(list, 0, item.id), first.id, item);
  }
  size_t hit = list.rows.size();
  for (size_t i = 0; i < list.rows.size(); ++i) {
    if (y >= list.rows[i].top && y < list.rows[i].top + list.rows[i].height) {
      hit = i;
      break;
    }
  }
  if (hit == list.rows.size()) {
    // In a gap between rows, or below the last: after the row above.
    size_t above = list.rows.size() - 1;
    for (size_t i = 0; i < list.rows.size(); ++i) {
      if (y >= list.rows[i].top + list.rows[i].height) {
        above = i;
      }
    }
    const TreeRow& row = list.rows[above].row;
    if (y >= list.rows.back().top + list.rows.back().height) {
      // Below everything: the end of the section.
      return Place(model, DropKind::kAfter, section, std::string(),
                   SiblingCount(list, std::string(), item.id),
                   list.rows.back().row.id, item);
    }
    return Place(model, DropKind::kAfter, section, row.parent,
                 SiblingIndex(list, above, item.id) + 1, row.id, item);
  }
  const DropRow& dr = list.rows[hit];
  const TreeRow& row = dr.row;
  const int before_index = SiblingIndex(list, hit, item.id);
  if (row.id == item.id) {
    // Over its own row: where it is already.
    return Place(model, DropKind::kBefore, section, row.parent, before_index,
                 row.id, item);
  }
  const float fraction =
      dr.height > 0 ? static_cast<float>(y - dr.top) / dr.height : 0.5f;
  const float edge = aerie::tokens::kMotionRatioFolderEdge;
  if (!row.is_folder) {
    const bool upper = fraction < 0.5f;
    return Place(model, upper ? DropKind::kBefore : DropKind::kAfter, section,
                 row.parent, before_index + (upper ? 0 : 1), row.id, item);
  }
  if (fraction < edge) {
    return Place(model, DropKind::kBefore, section, row.parent, before_index,
                 row.id, item);
  }
  if (fraction >= 1.0f - edge) {
    if (row.expanded) {
      return Place(model, DropKind::kFirstChild, section, row.id, 0, row.id,
                   item);
    }
    return Place(model, DropKind::kAfter, section, row.parent,
                 before_index + 1, row.id, item);
  }
  // The middle: into the folder, at its end. The item leaving this folder
  // does not count.
  int count = row.item_count;
  const aerie_tab_model::TabRecord* tab = model.GetTab(item.id);
  const aerie_tab_model::FolderRecord* folder = model.GetFolder(item.id);
  if ((item.kind == ItemKind::kTab && tab && tab->parent == row.id &&
       tab->section == section) ||
      (item.kind == ItemKind::kFolder && folder && folder->parent == row.id &&
       folder->section == section)) {
    --count;
  }
  return Place(model, DropKind::kInto, section, row.id, std::max(count, 0),
               row.id, item);
}

bool Inside(const DropRect& r, int x, int y) {
  return x >= r.x && x < r.x + r.width && y >= r.y && y < r.y + r.height;
}

DropTarget TargetInFavorites(const aerie_tab_model::TabModel& model, int x,
                             int y, const DropGeometry& geometry,
                             const DragItem& item) {
  if (item.kind == ItemKind::kFolder) {
    return Refused(Section::kFavorite, RefusalReason::kFolderInFavorites,
                   std::string());
  }
  const aerie_tab_model::TabRecord* tab = model.GetTab(item.id);
  if (!tab) {
    return DropTarget();
  }
  if (tab->section != Section::kFavorite) {
    size_t count = 0;
    for (const auto& [id, other] : model.tabs()) {
      if (other.section == Section::kFavorite &&
          other.workspace_id == tab->workspace_id) {
        ++count;
      }
    }
    if (count >= aerie_tab_model::kFavoritesCap) {
      return Refused(Section::kFavorite, RefusalReason::kFavoritesFull,
                     std::string());
    }
  }
  // Reading order: a tile precedes the pointer when it lies in a row above, or
  // in the same row and the pointer is past its middle.
  int index = 0;
  for (size_t i = 0; i < geometry.tiles.size(); ++i) {
    if (i < geometry.tile_ids.size() && geometry.tile_ids[i] == item.id) {
      continue;
    }
    const DropRect& t = geometry.tiles[i];
    if (y >= t.y + t.height ||
        (y >= t.y && x >= t.x + t.width / 2)) {
      ++index;
    }
  }
  DropTarget target;
  target.kind = DropKind::kFavoritesSlot;
  target.section = Section::kFavorite;
  target.index = index;
  return target;
}

}  // namespace

DropTarget ComputeDropTarget(const aerie_tab_model::TabModel& model,
                             int x,
                             int y,
                             const DropGeometry& geometry,
                             const DragItem& item) {
  // A divider wins over the rows and tiles next to it: it is the target of
  // "the end of this section", whether or not the section has anything in it.
  if (geometry.favorites_divider.height > 0 &&
      Inside(geometry.favorites_divider, x, y)) {
    // A pointer far below every tile gives the slot after the last one.
    DropTarget target = TargetInFavorites(
        model, x, std::numeric_limits<int>::max() / 2, geometry, item);
    target.on_divider = true;
    return target;
  }
  if (geometry.pinned_divider.height > 0 &&
      Inside(geometry.pinned_divider, x, y)) {
    DropList list = geometry.pinned;
    list.section = Section::kPinned;
    DropTarget target =
        TargetInList(model, list, std::numeric_limits<int>::max() / 2, item);
    target.on_divider = true;
    return target;
  }
  if (geometry.favorites_area.height > 0 &&
      Inside(geometry.favorites_area, x, y)) {
    return TargetInFavorites(model, x, y, geometry, item);
  }
  const DropList* lists[] = {&geometry.pinned, &geometry.today};
  const DropList* best = nullptr;
  int best_distance = 0;
  for (const DropList* list : lists) {
    if (list->bottom <= list->top) {
      continue;
    }
    const int distance = y < list->top ? list->top - y
                         : y >= list->bottom ? y - list->bottom + 1
                                             : 0;
    if (!best || distance < best_distance) {
      best = list;
      best_distance = distance;
    }
  }
  if (!best) {
    return DropTarget();
  }
  return TargetInList(model, *best, y, item);
}

}  // namespace aerie_sidebar
