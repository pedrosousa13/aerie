// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_AERIE_LOOK_LOOK_CROSSFADE_H_
#define CHROME_BROWSER_UI_VIEWS_AERIE_LOOK_LOOK_CROSSFADE_H_

#include <memory>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "components/aerie_design_tokens/aerie_motion.h"

class PrefService;

namespace ui {
class Layer;
class LayerTreeOwner;
}  // namespace ui

namespace views {
class Widget;
}

// The whole-chrome crossfade of a theme or mode change (issue #185,
// docs/sidebar-design.md "Themes"): two complete layers, so text is never
// half-faded on the wrong surface.
//
// Start() hands the window's current layers to a LayerTreeOwner (the way
// wm::RecreateLayers does for a window animation) and gives the window fresh
// layers. The snapshot keeps the pixels the window had, drawn above the new
// layers, and fades from opacity 1 to 0 over `motion-duration-xslow` on
// `motion-easing-standard`. That is one `opacity` animation on one layer, and
// nothing else is animated. When it ends the snapshot is destroyed, so no layer
// is left behind. The view hierarchy is not touched: the new look is applied to
// the views as usual and paints into the fresh layers underneath.
//
// A second Start() while the fade runs does not take another snapshot. The
// running animation is cancelled and the same snapshot fades again from the
// opacity it has reached.
//
// Nothing starts when reduced motion is on, for the operating system or for
// the app (the change applies at once), and a look applied at launch never
// calls Start().
//
// Mirror() is not used. A ui::Layer::Mirror() shows the source's current
// content, so it would fade into the new look, not out of the old one.
class LookCrossfade {
 public:
  explicit LookCrossfade(PrefService* prefs);
  LookCrossfade(const LookCrossfade&) = delete;
  LookCrossfade& operator=(const LookCrossfade&) = delete;
  ~LookCrossfade();

  // Returns true if a fade is running after the call. False: reduced motion, or
  // the window has no layer yet.
  bool Start(views::Widget* widget);

  bool running() const { return snapshot_ != nullptr; }
  // The opacity of the snapshot, 0 when there is none.
  float opacity() const;
  // The root layer of the snapshot, or null. For tests.
  ui::Layer* snapshot_layer_for_testing() const;
  // How many fades ended since this object was made. For tests.
  int finished_count() const { return finished_count_; }

 private:
  void FadeFrom(float opacity);
  void OnFadeEnded();

  const raw_ptr<PrefService> prefs_;
  std::unique_ptr<ui::LayerTreeOwner> snapshot_;
  aerie::motion::MotionHandle handle_;
  // Ends the fade when its duration has passed.
  base::OneShotTimer end_timer_;
  int finished_count_ = 0;
};

#endif  // CHROME_BROWSER_UI_VIEWS_AERIE_LOOK_LOOK_CROSSFADE_H_
