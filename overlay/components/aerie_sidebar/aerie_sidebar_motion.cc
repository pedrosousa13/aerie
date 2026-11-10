// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_sidebar/aerie_sidebar_motion.h"

namespace aerie_sidebar {

namespace {

using aerie::tokens::GetDurationMs;
using aerie::tokens::MotionDuration;
using aerie::tokens::MotionEasing;

SlideMotion Slide(MotionDuration duration,
                  MotionEasing easing,
                  int width,
                  int outer_edge_sign,
                  bool reduced,
                  float from_opacity,
                  float to_opacity) {
  SlideMotion motion;
  motion.duration = duration;
  motion.easing = easing;
  motion.animate_transform = !reduced;
  motion.hidden_x = outer_edge_sign *
                    (width + aerie::tokens::kMotionDistanceSidebarHideOffset);
  motion.animate_opacity = reduced;
  motion.from_opacity = from_opacity;
  motion.to_opacity = to_opacity;
  motion.duration_ms = GetDurationMs(duration, reduced);
  return motion;
}

}  // namespace

SlideMotion ShowMotion(int width, int outer_edge_sign, bool reduced) {
  return Slide(MotionDuration::kSlow, MotionEasing::kDecelerate, width,
               outer_edge_sign, reduced, 0.f, 1.f);
}

SlideMotion HideMotion(int width, int outer_edge_sign, bool reduced) {
  return Slide(MotionDuration::kBase, MotionEasing::kAccelerate, width,
               outer_edge_sign, reduced, 1.f, 0.f);
}

RowMotion OpenRowMotion(bool reduced) {
  RowMotion motion;
  motion.content_duration = MotionDuration::kFast;
  motion.content_easing = MotionEasing::kDecelerate;
  motion.content_duration_ms = GetDurationMs(motion.content_duration, reduced);
  motion.animate_scale = !reduced;
  motion.from_scale = aerie::tokens::kMotionScaleEnter;
  motion.animate_slide = false;
  motion.slide_x = 0;
  motion.from_opacity = 0.f;
  motion.to_opacity = 1.f;
  motion.animate_height = !reduced;
  motion.height_duration = MotionDuration::kBase;
  motion.height_easing = MotionEasing::kStandard;
  motion.height_duration_ms =
      reduced ? 0 : GetDurationMs(motion.height_duration, false);
  return motion;
}

RowMotion CloseRowMotion(int outer_edge_sign, bool reduced) {
  RowMotion motion;
  motion.content_duration = MotionDuration::kFast;
  motion.content_easing = MotionEasing::kAccelerate;
  motion.content_duration_ms = GetDurationMs(motion.content_duration, reduced);
  motion.animate_scale = false;
  motion.from_scale = 1.f;
  motion.animate_slide = !reduced;
  motion.slide_x =
      reduced ? 0 : outer_edge_sign * aerie::tokens::kMotionDistanceCloseSlide;
  motion.from_opacity = 1.f;
  motion.to_opacity = 0.f;
  motion.animate_height = !reduced;
  motion.height_duration = MotionDuration::kBase;
  motion.height_easing = MotionEasing::kStandard;
  motion.height_duration_ms =
      reduced ? 0 : GetDurationMs(motion.height_duration, false);
  return motion;
}

}  // namespace aerie_sidebar
