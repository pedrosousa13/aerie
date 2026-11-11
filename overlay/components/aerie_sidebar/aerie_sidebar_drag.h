// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_SIDEBAR_AERIE_SIDEBAR_DRAG_H_
#define COMPONENTS_AERIE_SIDEBAR_AERIE_SIDEBAR_DRAG_H_

#include <vector>

// The geometry of dragging a Today row to reorder it (issue #215, item 8,
// docs/sidebar-design.md, "Motion", row "Drag"). Headless: the view measures
// its rows once when the drag starts and asks these functions what the drag
// means. Distances are pixels along the list, positive downward.
//
// This is the basic form: a row moves inside its own list. Dragging between
// Today, Pinned, folders and Favorites is issue #182. It replaces
// DragTargetIndex with a hit test that also names the section and the parent
// folder, and keeps the rest (the dragged row follows the pointer, the others
// shift to make room, the dropped row settles).
namespace aerie_sidebar {

// One slot of the list: where a row rests and how tall it is. Slots are in
// list order, so `top` grows with the index.
struct DragSlot {
  int top = 0;
  int height = 0;
};

// How far the pointer has to travel before a press becomes a drag, so that a
// click that slips by a pixel still selects.
inline constexpr int kDragThreshold = 4;

// True once the pointer has moved far enough from the press point.
bool ExceedsDragThreshold(int dx, int dy);

// The offset of the dragged row `source` after the pointer moved `dy`, kept
// so that the row stays between the first and the last slot.
int ClampDragOffset(const std::vector<DragSlot>& slots, int source, int dy);

// The slot `source` would take if dropped now: the slot whose middle is the
// nearest to the middle of the row, which is at `dy` from its own slot.
// Returns `source` for a list of fewer than two slots.
int DragTargetIndex(const std::vector<DragSlot>& slots, int source, int dy);

// How far the row at `index` moves while `source` is held over `target`: the
// rows that the dragged row passes move into the slot it leaves (one slot
// toward it); every other row, and the dragged row itself, 0. The dragged
// row's own offset is the pointer's.
int DragShift(const std::vector<DragSlot>& slots,
              int source,
              int target,
              int index);

// Where the dropped row settles from: its visual offset from the slot it takes
// (the slot `target`), given the pointer offset `dy` from its old slot. The
// settle animates this to 0.
int SettleOffset(const std::vector<DragSlot>& slots,
                 int source,
                 int target,
                 int dy);

}  // namespace aerie_sidebar

#endif  // COMPONENTS_AERIE_SIDEBAR_AERIE_SIDEBAR_DRAG_H_
