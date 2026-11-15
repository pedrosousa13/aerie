// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/ui/views/aerie_look/look_crossfade.h"

#include "base/functional/bind.h"
#include "base/time/time.h"
#include "components/aerie_design_tokens/aerie_tokens.h"
#include "components/prefs/pref_service.h"
#include "ui/aura/window.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/layer_animator.h"
#include "ui/compositor/layer_tree_owner.h"
#include "ui/views/widget/widget.h"
#include "ui/wm/core/window_util.h"

LookCrossfade::LookCrossfade(PrefService* prefs) : prefs_(prefs) {}

LookCrossfade::~LookCrossfade() {
  Cancel();
}

void LookCrossfade::Cancel() {
  end_timer_.Stop();
  handle_.Abort();
  snapshot_.reset();
}

bool LookCrossfade::Start(views::Widget* widget) {
  if (aerie::motion::ShouldReduceMotion(prefs_)) {
    // A change applies at once: no snapshot, no animation. A fade that was
    // running ends here.
    if (snapshot_) {
      OnFadeEnded();
    }
    return false;
  }
  if (snapshot_) {
    // A second change: the same snapshot fades again from where it is.
    FadeFrom(snapshot_->root()->opacity());
    return true;
  }
  aura::Window* window = widget ? widget->GetNativeView() : nullptr;
  if (!window || !window->layer()) {
    return false;
  }
  // The window keeps fresh layers, which have not painted yet: the views paint
  // them in the next frame, with the new look. The old layers go to the owner
  // and stay above them.
  snapshot_ = wm::RecreateLayers(window);
  if (!snapshot_ || !snapshot_->root()) {
    snapshot_.reset();
    return false;
  }
  FadeFrom(1.0f);
  return true;
}

void LookCrossfade::FadeFrom(float opacity) {
  ui::Layer* layer = snapshot_->root();
  // Cancel the running fade; the layer keeps the value it reached.
  handle_.Abort();
  layer->SetOpacity(opacity);
  aerie::motion::LayerMotion target;
  target.opacity = 0.0f;
  handle_ = aerie::motion::AnimateLayer(
      layer, target, aerie::tokens::MotionDuration::kXslow,
      aerie::tokens::MotionEasing::kStandard, prefs_);
  end_timer_.Start(
      FROM_HERE, base::Milliseconds(aerie::tokens::kMotionDurationXslow),
      base::BindOnce(&LookCrossfade::OnFadeEnded, base::Unretained(this)));
}

void LookCrossfade::OnFadeEnded() {
  end_timer_.Stop();
  handle_.Abort();
  snapshot_.reset();
  ++finished_count_;
}

float LookCrossfade::opacity() const {
  return snapshot_ ? snapshot_->root()->opacity() : 0.0f;
}

ui::Layer* LookCrossfade::snapshot_layer_for_testing() const {
  return snapshot_ ? snapshot_->root() : nullptr;
}
