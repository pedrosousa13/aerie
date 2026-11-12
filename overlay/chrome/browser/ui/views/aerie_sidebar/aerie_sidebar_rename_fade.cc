// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_rename_fade.h"

#include "components/aerie_sidebar/aerie_sidebar_motion.h"
#include "ui/compositor/layer.h"
#include "ui/views/view.h"

namespace {

aerie::motion::MotionHandle Fade(views::View* view,
                                 const aerie_sidebar::RenameFadeMotion& motion,
                                 const PrefService* prefs) {
  if (!view->layer()) {
    view->SetPaintToLayer();
    view->layer()->SetFillsBoundsOpaquely(false);
  }
  view->layer()->SetOpacity(motion.from_opacity);
  // Opacity only: no transform is ever named here.
  aerie::motion::LayerMotion target;
  target.opacity = motion.to_opacity;
  return aerie::motion::AnimateLayer(view->layer(), target, motion.duration,
                                     motion.easing, prefs);
}

}  // namespace

aerie::motion::MotionHandle FadeRenameViewIn(
    views::View* view,
    const PrefService* reduced_motion_prefs) {
  return Fade(view, aerie_sidebar::RenameFade(), reduced_motion_prefs);
}

aerie::motion::MotionHandle FadeRenameViewOut(
    views::View* view,
    const PrefService* reduced_motion_prefs) {
  return Fade(view, aerie_sidebar::RenameFadeOut(), reduced_motion_prefs);
}
