// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_SIDEBAR_AERIE_SIDEBAR_DRAG_H_
#define COMPONENTS_AERIE_SIDEBAR_AERIE_SIDEBAR_DRAG_H_

// When a press on a row or a tile becomes a drag (issue #215, item 8). Where a
// drag drops is aerie_sidebar_drop_target.h (issue #182).
namespace aerie_sidebar {

// How far the pointer has to travel before a press becomes a drag, so that a
// click that slips by a pixel still selects.
inline constexpr int kDragThreshold = 4;

// True once the pointer has moved far enough from the press point.
bool ExceedsDragThreshold(int dx, int dy);

}  // namespace aerie_sidebar

#endif  // COMPONENTS_AERIE_SIDEBAR_AERIE_SIDEBAR_DRAG_H_
