// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_AERIE_HZ_TABS_AERIE_HZ_TAB_MOTION_H_
#define CHROME_BROWSER_UI_VIEWS_AERIE_HZ_TABS_AERIE_HZ_TAB_MOTION_H_

#include "components/aerie_design_tokens/aerie_motion.h"
#include "components/aerie_sidebar/aerie_sidebar_hz_policy.h"

class PrefService;

namespace views {
class View;
}  // namespace views

// The open and close motion of a tab in the horizontal strip (issue #186,
// docs/sidebar-design.md, "Horizontal tabs"). Only transform and opacity of the
// tab's layer are animated, through aerie::motion::AnimateLayer, so reduced
// motion (the operating system's flag and aerie.motion.reduce) is decided in
// the one place that already decides it. The strip's tab width is Chromium's
// own (TabCollectionAnimatingLayoutManager); WidthDurationMs gives it the
// token's duration.
namespace aerie_hz_tabs {

// The tab opens: it scales in from 0.96 and fades in over `tab-open`. Under
// reduced motion it only fades, in at most 140 ms, with no transform. `prefs`
// may be null (the operating system's flag only).
void AnimateOpen(views::View* tab, const PrefService* prefs);

// The tab closes: it slides 8 px down and fades over `xfast`. Under reduced
// motion it only fades, in at most 90 ms.
void AnimateClose(views::View* tab, const PrefService* prefs);

// The duration of the width animation that goes with the event: `tab-open`
// when a tab opens, `tab-close` when it closes, and the reduced durations
// under reduced motion.
int WidthDurationMs(aerie_sidebar::HzTabEvent event, const PrefService* prefs);

}  // namespace aerie_hz_tabs

#endif  // CHROME_BROWSER_UI_VIEWS_AERIE_HZ_TABS_AERIE_HZ_TAB_MOTION_H_
