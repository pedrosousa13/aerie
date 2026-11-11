// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_AERIE_TAB_MODEL_PINNED_CLOSE_HOOK_H_
#define CHROME_BROWSER_AERIE_TAB_MODEL_PINNED_CLOSE_HOOK_H_

namespace tabs {
class TabInterface;
}  // namespace tabs

namespace aerie_tab_model {

// The one call that chrome::CloseTab (IDC_CLOSE_TAB, Ctrl+W) makes before it
// closes the selected tab (patches/sidebar/0036). It returns true when `tab` is
// a Pinned or Favorites tab and the press was handled: the pinned-close setting
// decided what happens, and the caller does nothing more. This header has no
// dependencies, so that //chrome/browser/ui can include it without a cycle; the
// definition is in pinned_tabs_controller.cc.
bool HandlePinnedClose(tabs::TabInterface* tab);

}  // namespace aerie_tab_model

#endif  // CHROME_BROWSER_AERIE_TAB_MODEL_PINNED_CLOSE_HOOK_H_
