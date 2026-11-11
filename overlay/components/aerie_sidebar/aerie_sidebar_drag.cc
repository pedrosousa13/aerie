// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_sidebar/aerie_sidebar_drag.h"

#include <algorithm>
#include <cstdlib>

namespace aerie_sidebar {

namespace {

int Middle(const DragSlot& slot) {
  return slot.top + slot.height / 2;
}

bool Valid(const std::vector<DragSlot>& slots, int index) {
  return index >= 0 && index < static_cast<int>(slots.size());
}

}  // namespace

bool ExceedsDragThreshold(int dx, int dy) {
  return std::abs(dx) > kDragThreshold || std::abs(dy) > kDragThreshold;
}

int ClampDragOffset(const std::vector<DragSlot>& slots, int source, int dy) {
  if (!Valid(slots, source)) {
    return 0;
  }
  const int min = slots.front().top - slots[source].top;
  const int max = slots.back().top - slots[source].top;
  return std::clamp(dy, min, max);
}

int DragTargetIndex(const std::vector<DragSlot>& slots, int source, int dy) {
  if (!Valid(slots, source) || slots.size() < 2) {
    return source;
  }
  const int middle = Middle(slots[source]) + dy;
  int best = source;
  int best_distance = std::abs(middle - Middle(slots[source]));
  for (int i = 0; i < static_cast<int>(slots.size()); ++i) {
    const int distance = std::abs(middle - Middle(slots[i]));
    if (distance < best_distance) {
      best = i;
      best_distance = distance;
    }
  }
  return best;
}

int DragShift(const std::vector<DragSlot>& slots,
              int source,
              int target,
              int index) {
  if (!Valid(slots, source) || !Valid(slots, target) || !Valid(slots, index) ||
      index == source) {
    return 0;
  }
  if (target > source && index > source && index <= target) {
    return slots[index - 1].top - slots[index].top;
  }
  if (target < source && index < source && index >= target) {
    return slots[index + 1].top - slots[index].top;
  }
  return 0;
}

int SettleOffset(const std::vector<DragSlot>& slots,
                 int source,
                 int target,
                 int dy) {
  if (!Valid(slots, source) || !Valid(slots, target)) {
    return 0;
  }
  return slots[source].top + dy - slots[target].top;
}

}  // namespace aerie_sidebar
