// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_DESIGN_TOKENS_AERIE_MOTION_H_
#define COMPONENTS_AERIE_DESIGN_TOKENS_AERIE_MOTION_H_

#include <memory>
#include <optional>
#include <vector>

#include "base/memory/scoped_refptr.h"
#include "base/time/time.h"
#include "components/aerie_design_tokens/aerie_tokens.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/geometry/transform.h"

class PrefService;

namespace ui {
class Layer;
class LayerAnimationSequence;
class LayerAnimator;
}  // namespace ui

// The motion helper (issue #174, ADR 0007). Every Aerie animation of a layer
// goes through AnimateLayer, so reduced motion is decided in one place:
// ShouldReduceMotion().
//
// Which curves run how. ui::LayerAnimationElement takes only a
// gfx::Tween::Type, not a cubic bezier or a spring. The spike for this slice
// found that each of the four cubic-bezier tokens equals one Tween type
// exactly (standard = FAST_OUT_SLOW_IN_3, decelerate = ACCEL_5_70_DECEL_90,
// accelerate = ACCEL_30_DECEL_20_85, in-out = FAST_OUT_SLOW_IN, and linear =
// LINEAR), so those run as one element with that type, on the compositor
// thread. A sampling of 16 linear segments was measured too and was worse:
// up to 8.4% of the travel away from the curve for decelerate (1.9% for
// standard) against 0 for the exact type. Only the spring tokens have no
// Tween type; they chain one linear element per interval of their sampled
// curve (the same points as the CSS linear() curve), which also runs on the
// compositor thread. A bezier that matches no Tween type (a changed token) is
// sampled into 16 linear segments, so a token edit still animates correctly.
namespace aerie::motion {

// Under reduced motion no opacity element is longer than this.
inline constexpr int kMaxReducedOpacityMs = 140;

// True when the operating system asks for reduced motion
// (gfx::Animation::PrefersReducedMotion) or the pref aerie.motion.reduce is
// "on". `prefs` may be null (system setting only).
bool ShouldReduceMotion(const PrefService* prefs);

// What to animate. Leave a field empty to leave that property alone.
struct LayerMotion {
  std::optional<gfx::Transform> transform;
  std::optional<float> opacity;
};

// One linear interval of a sampled curve: it lasts `duration` and ends at
// `progress` (0 is the start value, 1 the target).
struct MotionSegment {
  base::TimeDelta duration;
  double progress = 0;
};

// What AnimateLayer will do, as plain data (so tests can check it).
struct MotionPlan {
  // Animate this property with elements over `duration`.
  bool animate_transform = false;
  bool animate_opacity = false;
  // Set this property to its target at once, without an element. Reduced
  // motion does this for the transform: it is never animated.
  bool set_transform_now = false;
  bool set_opacity_now = false;
  base::TimeDelta duration;
  // Empty: one element per property that uses `tween`. Otherwise one linear
  // element per segment.
  gfx::Tween::Type tween = gfx::Tween::LINEAR;
  std::vector<MotionSegment> segments;

  MotionPlan();
  MotionPlan(const MotionPlan&);
  MotionPlan& operator=(const MotionPlan&);
  ~MotionPlan();
};

MotionPlan PlanMotion(const LayerMotion& motion,
                      tokens::MotionDuration duration,
                      tokens::MotionEasing easing,
                      bool reduced);

// PlanMotion for a duration that is not one of the `motion-duration` tokens:
// the spring settle of a dropped row uses `motion-spring-duration` (420 ms,
// 140 ms reduced), which has no MotionDuration. `reduced_duration_ms` is used
// when `reduced` is true.
MotionPlan PlanMotionForMs(const LayerMotion& motion,
                           int duration_ms,
                           int reduced_duration_ms,
                           tokens::MotionEasing easing,
                           bool reduced);

// The sequences of a plan: one for the transform and one for the opacity, when
// the plan animates them. They animate from the given values to the targets.
std::vector<std::unique_ptr<ui::LayerAnimationSequence>> BuildSequences(
    const MotionPlan& plan,
    const LayerMotion& motion,
    const gfx::Transform& from_transform,
    float from_opacity);

// Aborts the animation that AnimateLayer started. The layer keeps the value it
// has reached.
class MotionHandle {
 public:
  MotionHandle();
  explicit MotionHandle(scoped_refptr<ui::LayerAnimator> animator);
  MotionHandle(const MotionHandle&);
  MotionHandle& operator=(const MotionHandle&);
  ~MotionHandle();

  // Aborts every running animation of the layer: Aerie code is the one owner
  // of a layer's transform and opacity animations.
  void Abort();
  bool IsAnimating() const;

 private:
  scoped_refptr<ui::LayerAnimator> animator_;
};

// Starts a plan on an animator. One owner per animation: a running transform
// or opacity animation is aborted first, and the new one starts from the
// values given (the layer's current ones). Under reduced motion the
// transform is set at once.
MotionHandle StartMotion(ui::LayerAnimator* animator,
                         const MotionPlan& plan,
                         const LayerMotion& motion,
                         const gfx::Transform& from_transform,
                         float from_opacity);

// Animates `layer` to `motion` over the duration token on the easing token.
// Only `transform` and `opacity` are ever animated. Reduced motion
// (ShouldReduceMotion(prefs)) uses the token's reduced duration, never
// animates the transform and never runs an opacity animation longer than
// kMaxReducedOpacityMs.
MotionHandle AnimateLayer(ui::Layer* layer,
                          const LayerMotion& motion,
                          tokens::MotionDuration duration,
                          tokens::MotionEasing easing,
                          const PrefService* prefs);

// AnimateLayer for a duration in ms (see PlanMotionForMs).
MotionHandle AnimateLayerForMs(ui::Layer* layer,
                               const LayerMotion& motion,
                               int duration_ms,
                               int reduced_duration_ms,
                               tokens::MotionEasing easing,
                               const PrefService* prefs);

}  // namespace aerie::motion

#endif  // COMPONENTS_AERIE_DESIGN_TOKENS_AERIE_MOTION_H_
