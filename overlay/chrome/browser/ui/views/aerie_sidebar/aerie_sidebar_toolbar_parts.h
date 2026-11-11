// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_TOOLBAR_PARTS_H_
#define CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_TOOLBAR_PARTS_H_

#include "base/memory/raw_ptr.h"

namespace views {
class View;
}  // namespace views

// What the sidebar takes out of the browser's toolbar (issue #215). The views
// are the toolbar's own, not copies: the address pill is the real location
// bar, so the omnibox, the page actions and the Guard icon keep working, and
// the buttons keep their commands and menus. All are plain views, and this
// header only forward-declares them, so that the frame code that hands them
// over (BrowserView, VerticalTabStripRegionView) needs nothing else.
struct AerieSidebarToolbarParts {
  raw_ptr<views::View> toolbar;       // What stays after the rest left.
  raw_ptr<views::View> location_bar;  // Goes into the address pill.
  raw_ptr<views::View> back;          // These three go into the top row.
  raw_ptr<views::View> forward;
  raw_ptr<views::View> reload;
};

#endif  // CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_TOOLBAR_PARTS_H_
