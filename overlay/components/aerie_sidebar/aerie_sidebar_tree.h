// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_SIDEBAR_AERIE_SIDEBAR_TREE_H_
#define COMPONENTS_AERIE_SIDEBAR_AERIE_SIDEBAR_TREE_H_

#include <string>
#include <vector>

#include "components/aerie_sidebar/aerie_sidebar_today_list.h"
#include "components/aerie_tab_model/folder_tree.h"

// What the Pinned and Today trees decide without a view (issue #181,
// docs/sidebar-design.md, "Keyboard and screen reader").
namespace aerie_sidebar {

struct TreeKeyResult {
  // The row that holds the roving tab stop afterwards; -1 for an empty tree.
  int focus_index = -1;
  // Delete: close the tab of the focused row. Never set on a folder row.
  bool close_focused = false;
  // Right, Left and Toggle: the folder to open or shut. Empty when no folder
  // changes.
  std::string toggle_folder_id;
  bool expand = false;
};

// One key on the row at `focused` of the visible `rows`.
//   Up, Down, Home, End: move the stop, no wrap.
//   Right: on a shut folder open it; on an open folder with children move to
//          the first child; anywhere else nothing.
//   Left: on an open folder shut it; on a shut folder or a tab move to the
//         parent folder's row (nothing at the top level).
//   Toggle: on a folder open it when shut and shut it when open.
//   Delete: close a tab row; on a folder row nothing happens.
TreeKeyResult HandleTreeKey(const std::vector<aerie_tab_model::TreeRow>& rows,
                            int focused,
                            ListKey key);

// The row of `rows` to hold the stop after the folder `folder_id` shuts, when
// the stop was on a row inside it: the folder's own row. Otherwise `focused`
// still holds (the index in the rows after the change is the caller's to
// look up). Returns the folder's index in `rows`, or -1 when `focused` is not
// inside the folder.
int StopAfterCollapse(const std::vector<aerie_tab_model::TreeRow>& rows_before,
                      const std::string& folder_id,
                      int focused);

// The delay, in ms, before the child at `index` (0 for the first revealed
// row) starts to fade in: `motion-timing-stagger` apart for the first
// `motion-timing-stagger-cap` children, and the last of those delays after
// that. Zero under reduced motion.
int RevealStaggerDelayMs(int index, bool reduced);

}  // namespace aerie_sidebar

#endif  // COMPONENTS_AERIE_SIDEBAR_AERIE_SIDEBAR_TREE_H_
