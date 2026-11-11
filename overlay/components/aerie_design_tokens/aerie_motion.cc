// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_design_tokens/aerie_motion.h"

#include <algorithm>
#include <utility>

#include "components/aerie_design_tokens/aerie_motion_prefs.h"
#include "components/prefs/pref_service.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/layer_animation_element.h"
#include "ui/compositor/layer_animation_sequence.h"
#include "ui/compositor/layer_animator.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/geometry/cubic_bezier.h"

namespace aerie::motion {

namespace {

// The cubic-bezier tokens and the gfx::Tween::Type that has the same four
// numbers (ui/gfx/animation/tween.cc). aerie_design_tokens_unittest.cc checks
// each pair against gfx::CubicBezier.
struct TweenForCurve {
  tokens::CubicBezier curve;
  gfx::Tween::Type type;
};

constexpr TweenForCurve kTweenForCurve[] = {
    {tokens::kMotionEasingStandard, gfx::Tween::FAST_OUT_SLOW_IN_3},
    {tokens::kMotionEasingDecelerate, gfx::Tween::ACCEL_5_70_DECEL_90},
    {tokens::kMotionEasingAccelerate, gfx::Tween::ACCEL_30_DECEL_20_85},
    {tokens::kMotionEasingInOut, gfx::Tween::FAST_OUT_SLOW_IN},
    {tokens::kMotionEasingLinear, gfx::Tween::LINEAR},
};

// Segments for a cubic bezier that matches no Tween type.
constexpr int kBezierSegments = 16;

bool SameCurve(const tokens::CubicBezier& a, const tokens::CubicBezier& b) {
  return a.x1 == b.x1 && a.y1 == b.y1 && a.x2 == b.x2 && a.y2 == b.y2;
}

base::TimeDelta Fraction(base::TimeDelta total, int numerator, int denominator) {
  return base::Microseconds(total.InMicroseconds() * numerator / denominator);
}

// Splits `total` into segments that end at the given progress values.
std::vector<MotionSegment> Segments(base::TimeDelta total,
                                    const std::vector<double>& progress) {
  std::vector<MotionSegment> segments;
  const int count = static_cast<int>(progress.size());
  base::TimeDelta previous_end;
  for (int i = 0; i < count; ++i) {
    const base::TimeDelta end = Fraction(total, i + 1, count);
    segments.push_back({end - previous_end, progress[i]});
    previous_end = end;
  }
  return segments;
}

void PlanCurve(MotionPlan& plan, const tokens::Easing& easing) {
  if (easing.sampled) {
    // Sample 0 is the start (progress 0): the segments end at samples 1...n-1.
    std::vector<double> progress(easing.samples + 1,
                                 easing.samples + easing.sample_count);
    plan.segments = Segments(plan.duration, progress);
    return;
  }
  for (const TweenForCurve& match : kTweenForCurve) {
    if (SameCurve(match.curve, easing.bezier)) {
      plan.tween = match.type;
      return;
    }
  }
  const gfx::CubicBezier bezier(easing.bezier.x1, easing.bezier.y1,
                                easing.bezier.x2, easing.bezier.y2);
  std::vector<double> progress;
  for (int i = 1; i <= kBezierSegments; ++i) {
    progress.push_back(bezier.Solve(static_cast<double>(i) / kBezierSegments));
  }
  plan.segments = Segments(plan.duration, progress);
}

template <typename MakeElement>
std::unique_ptr<ui::LayerAnimationSequence> BuildSequence(
    const MotionPlan& plan,
    MakeElement make_element) {
  auto sequence = std::make_unique<ui::LayerAnimationSequence>();
  if (plan.segments.empty()) {
    std::unique_ptr<ui::LayerAnimationElement> element =
        make_element(1.0, plan.duration);
    element->set_tween_type(plan.tween);
    sequence->AddElement(std::move(element));
    return sequence;
  }
  for (const MotionSegment& segment : plan.segments) {
    std::unique_ptr<ui::LayerAnimationElement> element =
        make_element(segment.progress, segment.duration);
    element->set_tween_type(gfx::Tween::LINEAR);
    sequence->AddElement(std::move(element));
  }
  return sequence;
}

}  // namespace

bool ShouldReduceMotion(const PrefService* prefs) {
  if (gfx::Animation::PrefersReducedMotion()) {
    return true;
  }
  return prefs && prefs->GetString(kReduceMotionPref) == kReduceMotionOn;
}

MotionPlan::MotionPlan() = default;
MotionPlan::MotionPlan(const MotionPlan&) = default;
MotionPlan& MotionPlan::operator=(const MotionPlan&) = default;
MotionPlan::~MotionPlan() = default;

MotionPlan PlanMotion(const LayerMotion& motion,
                      tokens::MotionDuration duration,
                      tokens::MotionEasing easing,
                      bool reduced) {
  return PlanMotionForMs(motion, tokens::GetDurationMs(duration, false),
                         tokens::GetDurationMs(duration, true), easing,
                         reduced);
}

MotionPlan PlanMotionForMs(const LayerMotion& motion,
                           int duration_ms,
                           int reduced_duration_ms,
                           tokens::MotionEasing easing,
                           bool reduced) {
  MotionPlan plan;
  int ms = reduced ? reduced_duration_ms : duration_ms;
  if (reduced) {
    ms = std::min(ms, kMaxReducedOpacityMs);
  }
  plan.duration = base::Milliseconds(ms);

  const bool animated = ms > 0;
  if (motion.transform) {
    // Reduced motion never animates a transform.
    plan.animate_transform = animated && !reduced;
    plan.set_transform_now = !plan.animate_transform;
  }
  if (motion.opacity) {
    plan.animate_opacity = animated;
    plan.set_opacity_now = !animated;
  }
  if (plan.animate_transform || plan.animate_opacity) {
    PlanCurve(plan, tokens::GetEasing(easing, reduced));
  }
  return plan;
}

std::vector<std::unique_ptr<ui::LayerAnimationSequence>> BuildSequences(
    const MotionPlan& plan,
    const LayerMotion& motion,
    const gfx::Transform& from_transform,
    float from_opacity) {
  std::vector<std::unique_ptr<ui::LayerAnimationSequence>> sequences;
  if (plan.animate_transform && motion.transform) {
    const gfx::Transform target = *motion.transform;
    sequences.push_back(BuildSequence(
        plan, [&](double progress, base::TimeDelta duration) {
          return ui::LayerAnimationElement::CreateTransformElement(
              gfx::Tween::TransformValueBetween(progress, from_transform,
                                                target),
              duration);
        }));
  }
  if (plan.animate_opacity && motion.opacity) {
    const float target = *motion.opacity;
    sequences.push_back(BuildSequence(
        plan, [&](double progress, base::TimeDelta duration) {
          return ui::LayerAnimationElement::CreateOpacityElement(
              // A spring overshoots 1: opacity stays in range.
              std::clamp(
                  gfx::Tween::FloatValueBetween(progress, from_opacity, target),
                  0.0f, 1.0f),
              duration);
        }));
  }
  return sequences;
}

MotionHandle::MotionHandle() = default;
MotionHandle::MotionHandle(scoped_refptr<ui::LayerAnimator> animator)
    : animator_(std::move(animator)) {}
MotionHandle::MotionHandle(const MotionHandle&) = default;
MotionHandle& MotionHandle::operator=(const MotionHandle&) = default;
MotionHandle::~MotionHandle() = default;

void MotionHandle::Abort() {
  if (animator_) {
    animator_->AbortAllAnimations();
  }
}

bool MotionHandle::IsAnimating() const {
  return animator_ && animator_->is_animating();
}

MotionHandle StartMotion(ui::LayerAnimator* animator,
                         const MotionPlan& plan,
                         const LayerMotion& motion,
                         const gfx::Transform& from_transform,
                         float from_opacity) {
  // One owner per animation: the running one is aborted. Transform and opacity
  // run on the compositor, so the value the main thread holds stays what it
  // last had; the new animation starts from the values passed in.
  if (animator->IsAnimatingOnePropertyOf(
          ui::LayerAnimationElement::TRANSFORM |
          ui::LayerAnimationElement::OPACITY)) {
    animator->AbortAllAnimations();
  }
  if (plan.set_transform_now && motion.transform) {
    animator->SetTransform(*motion.transform);
  }
  if (plan.set_opacity_now && motion.opacity) {
    animator->SetOpacity(*motion.opacity);
  }

  std::vector<std::unique_ptr<ui::LayerAnimationSequence>> sequences =
      BuildSequences(plan, motion, from_transform, from_opacity);
  if (sequences.size() == 1) {
    animator->StartAnimation(sequences[0].release());
  } else if (!sequences.empty()) {
    // Both start together, so they share one start time.
    std::vector<ui::LayerAnimationSequence*> raw;
    for (auto& sequence : sequences) {
      raw.push_back(sequence.release());
    }
    animator->StartTogether(raw);
  }
  return MotionHandle(scoped_refptr<ui::LayerAnimator>(animator));
}

MotionHandle AnimateLayer(ui::Layer* layer,
                          const LayerMotion& motion,
                          tokens::MotionDuration duration,
                          tokens::MotionEasing easing,
                          const PrefService* prefs) {
  return AnimateLayerForMs(layer, motion,
                           tokens::GetDurationMs(duration, false),
                           tokens::GetDurationMs(duration, true), easing,
                           prefs);
}

MotionHandle AnimateLayerForMs(ui::Layer* layer,
                               const LayerMotion& motion,
                               int duration_ms,
                               int reduced_duration_ms,
                               tokens::MotionEasing easing,
                               const PrefService* prefs) {
  const MotionPlan plan =
      PlanMotionForMs(motion, duration_ms, reduced_duration_ms, easing,
                      ShouldReduceMotion(prefs));
  return StartMotion(layer->GetAnimator(), plan, motion, layer->transform(),
                     layer->opacity());
}

}  // namespace aerie::motion
