// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/ui/views/aerie_hz_tabs/aerie_hz_tab_motion.h"

#include "components/aerie_design_tokens/aerie_tokens.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/views/view.h"

namespace aerie_hz_tabs {

namespace {

// A tab opens from this scale (the design's "scales in").
constexpr float kOpenScale = 0.96f;

}  // namespace

void AnimateOpen(views::View* tab, const PrefService* prefs) {
  const bool reduced = aerie::motion::ShouldReduceMotion(prefs);
  const aerie_sidebar::HzTabMotion plan =
      aerie_sidebar::PlanHzTabMotion(aerie_sidebar::HzTabEvent::kOpen, reduced);
  tab->SetPaintToLayer();
  ui::Layer* const layer = tab->layer();
  layer->SetFillsBoundsOpaquely(false);
  layer->SetOpacity(0.0f);
  if (plan.animate_transform) {
    // Scale about the middle of the tab's height; the width is growing from 0
    // at the same time, so the left edge is the anchor.
    gfx::Transform from;
    from.Translate(0, tab->GetPreferredSize().height() * (1.0f - kOpenScale) / 2);
    from.Scale(kOpenScale, kOpenScale);
    layer->SetTransform(from);
  }
  aerie::motion::LayerMotion target;
  target.opacity = 1.0f;
  target.transform = gfx::Transform();
  aerie::motion::AnimateLayer(layer, target,
                              aerie::tokens::MotionDuration::kTabOpen,
                              aerie::tokens::MotionEasing::kDecelerate, prefs);
}

void AnimateClose(views::View* tab, const PrefService* prefs) {
  const bool reduced = aerie::motion::ShouldReduceMotion(prefs);
  const aerie_sidebar::HzTabMotion plan = aerie_sidebar::PlanHzTabMotion(
      aerie_sidebar::HzTabEvent::kClose, reduced);
  tab->SetPaintToLayer();
  ui::Layer* const layer = tab->layer();
  layer->SetFillsBoundsOpaquely(false);
  aerie::motion::LayerMotion target;
  target.opacity = 0.0f;
  if (plan.animate_transform) {
    gfx::Transform slide;
    slide.Translate(0, plan.slide_px);
    target.transform = slide;
  }
  aerie::motion::AnimateLayer(layer, target,
                              aerie::tokens::MotionDuration::kXfast,
                              aerie::tokens::MotionEasing::kStandard, prefs);
}

int WidthDurationMs(aerie_sidebar::HzTabEvent event, const PrefService* prefs) {
  return aerie_sidebar::PlanHzTabMotion(event,
                                        aerie::motion::ShouldReduceMotion(prefs))
      .width_duration_ms;
}

}  // namespace aerie_hz_tabs
