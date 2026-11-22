// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_CARD_FADE_H_
#define CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_CARD_FADE_H_

#include "components/aerie_design_tokens/aerie_motion.h"
#include "components/aerie_sidebar/aerie_sidebar_motion.h"

class PrefService;

namespace views {
class View;
}

namespace aerie_sidebar {

// How the toast and the media card of the sidebar come and go (docs/sidebar-
// design.md, "Toast" and "Media card"): the card rises while it fades in, and
// fades out. The caller owns the timer that ends a fade and calls
// EndCardFade() then, and it owns the returned handle.
//
// Shows `card` on a layer of its own (made here when it has none), and starts
// the fade in of `motion`.
aerie::motion::MotionHandle StartCardFadeIn(views::View* card,
                                            const ToastMotion& motion,
                                            const PrefService* prefs);
// Starts the fade out of `motion` on the layer of `card`.
aerie::motion::MotionHandle StartCardFadeOut(views::View* card,
                                             const ToastMotion& motion,
                                             const PrefService* prefs);
// Hides `card` and drops its layer, after the fade out.
void EndCardFade(views::View* card);

}  // namespace aerie_sidebar

#endif  // CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_CARD_FADE_H_
