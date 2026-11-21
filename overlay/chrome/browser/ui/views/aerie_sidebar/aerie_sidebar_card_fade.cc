// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_card_fade.h"

#include "ui/compositor/layer.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/views/view.h"

namespace aerie_sidebar {

aerie::motion::MotionHandle StartCardFadeIn(views::View* card,
                                            const ToastMotion& motion,
                                            const PrefService* prefs) {
  card->SetVisible(true);
  if (!card->layer()) {
    card->SetPaintToLayer();
    card->layer()->SetFillsBoundsOpaquely(false);
  }
  ui::Layer* layer = card->layer();
  layer->SetOpacity(motion.from_opacity);
  aerie::motion::LayerMotion target;
  target.opacity = motion.to_opacity;
  if (motion.animate_rise) {
    layer->SetTransform(gfx::Transform::MakeTranslation(0, motion.rise_px));
    target.transform = gfx::Transform();
  } else {
    layer->SetTransform(gfx::Transform());
  }
  return aerie::motion::AnimateLayer(layer, target, motion.duration,
                                     motion.easing, prefs);
}

aerie::motion::MotionHandle StartCardFadeOut(views::View* card,
                                             const ToastMotion& motion,
                                             const PrefService* prefs) {
  aerie::motion::LayerMotion target;
  target.opacity = motion.to_opacity;
  return aerie::motion::AnimateLayer(card->layer(), target, motion.duration,
                                     motion.easing, prefs);
}

void EndCardFade(views::View* card) {
  card->SetVisible(false);
  if (card->layer()) {
    card->DestroyLayer();
  }
}

}  // namespace aerie_sidebar
