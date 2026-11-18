// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_sidebar/aerie_sidebar_hz_active.h"

#include <optional>

#include "components/aerie_sidebar/aerie_sidebar_density.h"
#include "components/aerie_sidebar/aerie_sidebar_hz_policy.h"
#include "components/aerie_sidebar/aerie_sidebar_layout.h"

namespace aerie_sidebar {

void UpdateActiveHzMetrics(const PrefService& prefs) {
  const TabLayout layout = GetTabLayout(prefs);
  if (layout == TabLayout::kSidebar) {
    SetActiveHzMetrics(std::nullopt);
    return;
  }
  SetActiveHzMetrics(GetHzMetrics(
      layout == TabLayout::kHorizontalOneRow ? HzRows::kOne : HzRows::kTwo,
      GetDensity(prefs) == Density::kCompact ? HzDensity::kCompact
                                            : HzDensity::kComfortable));
}

}  // namespace aerie_sidebar
