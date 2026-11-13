// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_ROW_ENTRY_H_
#define CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_ROW_ENTRY_H_

#include <string>

#include "base/memory/raw_ptr.h"

class AerieSidebarRowView;

// One row of a tree of the sidebar (Today or Pinned): the record it shows and
// its view. The sidebar view and the folder controller (issue #228) both keep
// lists of these.
struct AerieSidebarRowEntry {
  // The record id of the tab, or of the folder when `is_folder`.
  std::string tab_id;
  raw_ptr<AerieSidebarRowView> view;
  bool is_folder = false;
  int item_count = 0;
};

#endif  // CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_ROW_ENTRY_H_
