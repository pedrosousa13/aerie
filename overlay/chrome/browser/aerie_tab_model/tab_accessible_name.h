// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_AERIE_TAB_MODEL_TAB_ACCESSIBLE_NAME_H_
#define CHROME_BROWSER_AERIE_TAB_MODEL_TAB_ACCESSIBLE_NAME_H_

#include <string>

namespace tabs {
class TabInterface;
}  // namespace tabs

namespace aerie_tab_model {

// The accessible name of a tab of Chromium's horizontal strip, made the way the
// sidebar names its rows (issue #186): the name the user gave the tab if it has
// one, else `title`; then ", asleep" for a sleeping tab and ", renamed" for a
// tab with a name of the user's own. Called by
// tabs::GetAccessibleTabLabel (patches/sidebar/0055). A tab with no record, or a
// profile with no Tab Model, keeps `title`.
std::u16string AccessibleTabName(const tabs::TabInterface* tab,
                                 const std::u16string& title);

}  // namespace aerie_tab_model

#endif  // CHROME_BROWSER_AERIE_TAB_MODEL_TAB_ACCESSIBLE_NAME_H_
