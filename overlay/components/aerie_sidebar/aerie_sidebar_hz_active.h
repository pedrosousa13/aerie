// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_SIDEBAR_AERIE_SIDEBAR_HZ_ACTIVE_H_
#define COMPONENTS_AERIE_SIDEBAR_AERIE_SIDEBAR_HZ_ACTIVE_H_

class PrefService;

namespace aerie_sidebar {

// Sets the active horizontal metrics (SetActiveHzMetrics) from the profile's
// layout and density prefs: nothing in the sidebar layout, else the metrics of
// the layout's rows at Compact or Comfortable (Rail and Auto-hide are laid out
// as Comfortable, as in the sidebar). Called from the code that watches the
// layout pref (patches/sidebar/0059), once at start and again on a change of
// either pref.
void UpdateActiveHzMetrics(const PrefService& prefs);

}  // namespace aerie_sidebar

#endif  // COMPONENTS_AERIE_SIDEBAR_AERIE_SIDEBAR_HZ_ACTIVE_H_
