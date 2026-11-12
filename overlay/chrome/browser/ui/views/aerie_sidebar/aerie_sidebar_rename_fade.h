// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_RENAME_FADE_H_
#define CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_RENAME_FADE_H_

#include "components/aerie_design_tokens/aerie_motion.h"

class PrefService;

namespace views {
class View;
}  // namespace views

// The fades of the tab rename (issue #145), in one place: opacity only, on
// `xfast` (aerie_sidebar::RenameFade), through aerie::motion::AnimateLayer.
// Both give `view` a layer if it has none. The caller owns the handle and
// aborts it before it destroys the view's layer.
aerie::motion::MotionHandle FadeRenameViewIn(
    views::View* view,
    const PrefService* reduced_motion_prefs);
aerie::motion::MotionHandle FadeRenameViewOut(
    views::View* view,
    const PrefService* reduced_motion_prefs);

#endif  // CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_RENAME_FADE_H_
