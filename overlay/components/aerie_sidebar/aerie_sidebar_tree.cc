// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_sidebar/aerie_sidebar_tree.h"

#include <algorithm>

#include "components/aerie_design_tokens/aerie_tokens.h"

namespace aerie_sidebar {

TreeKeyResult HandleTreeKey(const std::vector<aerie_tab_model::TreeRow>& rows,
                            int focused,
                            ListKey key) {
  const int count = static_cast<int>(rows.size());
  TreeKeyResult result;
  if (count <= 0) {
    return result;
  }
  const int at = (focused < 0 || focused >= count) ? 0 : focused;
  const aerie_tab_model::TreeRow& row = rows[at];
  result.focus_index = at;
  switch (key) {
    case ListKey::kUp:
    case ListKey::kDown:
    case ListKey::kHome:
    case ListKey::kEnd:
      result.focus_index = HandleListKey(count, at, key).focus_index;
      break;
    case ListKey::kDelete:
      if (!row.is_folder) {
        const ListKeyResult list = HandleListKey(count, at, key);
        result.close_focused = true;
        result.focus_index = list.focus_index;
      }
      break;
    case ListKey::kRight:
      if (row.is_folder) {
        if (!row.expanded) {
          result.toggle_folder_id = row.id;
          result.expand = true;
        } else if (at + 1 < count && rows[at + 1].parent == row.id) {
          result.focus_index = at + 1;
        }
      }
      break;
    case ListKey::kLeft:
      if (row.is_folder && row.expanded) {
        result.toggle_folder_id = row.id;
        result.expand = false;
      } else if (!row.parent.empty()) {
        for (int i = at - 1; i >= 0; --i) {
          if (rows[i].is_folder && rows[i].id == row.parent) {
            result.focus_index = i;
            break;
          }
        }
      }
      break;
    case ListKey::kToggle:
      if (row.is_folder) {
        result.toggle_folder_id = row.id;
        result.expand = !row.expanded;
      }
      break;
  }
  return result;
}

int StopAfterCollapse(const std::vector<aerie_tab_model::TreeRow>& rows_before,
                      const std::string& folder_id,
                      int focused) {
  const int count = static_cast<int>(rows_before.size());
  int folder_index = -1;
  for (int i = 0; i < count; ++i) {
    if (rows_before[i].is_folder && rows_before[i].id == folder_id) {
      folder_index = i;
      break;
    }
  }
  if (folder_index < 0 || focused <= folder_index || focused >= count) {
    return -1;
  }
  // Rows inside the folder follow it with a deeper level.
  const int level = rows_before[folder_index].level;
  for (int i = folder_index + 1; i <= focused; ++i) {
    if (rows_before[i].level <= level) {
      return -1;
    }
  }
  return folder_index;
}

int RevealStaggerDelayMs(int index, bool reduced) {
  if (reduced || index < 0) {
    return 0;
  }
  const int last = aerie::tokens::kMotionTimingStaggerCap - 1;
  return std::min(index, last) * aerie::tokens::kMotionTimingStagger;
}

}  // namespace aerie_sidebar
