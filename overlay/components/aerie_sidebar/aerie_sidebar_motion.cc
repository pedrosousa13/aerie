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

SleepMotion SleepFaviconMotion(bool reduced) {
  SleepMotion motion;
  motion.animate_fade = !reduced;
  motion.duration = MotionDuration::kFast;
  motion.easing = MotionEasing::kStandard;
  motion.asleep_opacity = aerie::tokens::kMotionOpacitySleeping;
  return motion;
}

DragMotion RowDragMotion(bool reduced) {
  DragMotion motion;
  motion.animate_lift = !reduced;
  motion.lift_scale = aerie::tokens::kMotionScaleLift;
  motion.lift_duration = MotionDuration::kFast;
  motion.lift_easing = MotionEasing::kStandard;
  motion.animate_shift = !reduced;
  motion.shift_duration = MotionDuration::kBase;
  motion.shift_easing = MotionEasing::kStandard;
  motion.animate_settle = !reduced;
  motion.settle_duration_ms = aerie::tokens::kMotionSpringDuration;
  motion.settle_reduced_duration_ms = aerie::tokens::kMotionSpringDurationReduced;
  motion.settle_easing = MotionEasing::kSpring;
  return motion;
}

PinMotion PinFlipMotion(bool reduced) {
  PinMotion motion;
  motion.animate = !reduced;
  motion.moved_duration_ms = aerie::tokens::kMotionSpringDuration;
  motion.moved_reduced_duration_ms = aerie::tokens::kMotionSpringDurationReduced;
  motion.moved_easing = MotionEasing::kSpring;
  motion.others_duration = MotionDuration::kBase;
  motion.others_easing = MotionEasing::kStandard;
  return motion;
}

MenuOpenMotion ContextMenuOpenMotion(bool reduced) {
  MenuOpenMotion motion;
  motion.animate_scale = !reduced;
  motion.from_scale = aerie::tokens::kMotionScaleMenuEnter;
  motion.from_opacity = 0.f;
  motion.duration = MotionDuration::kFast;
  motion.easing = MotionEasing::kDecelerate;
  motion.duration_ms = GetDurationMs(motion.duration, reduced);
  return motion;
}

ToastMotion ToastShowMotion(bool reduced) {
  ToastMotion motion;
  motion.animate_rise = !reduced;
  motion.rise_px = reduced ? 0 : aerie::tokens::kMotionDistanceToastRise;
  motion.from_opacity = 0.f;
  motion.to_opacity = 1.f;
  motion.duration = MotionDuration::kFast;
  motion.easing = MotionEasing::kDecelerate;
  motion.duration_ms = GetDurationMs(motion.duration, reduced);
  motion.hold_ms = reduced ? aerie::tokens::kMotionTimingToastReduced
                           : aerie::tokens::kMotionTimingToast;
  return motion;
}

ToastMotion ToastHideMotion(bool reduced) {
  ToastMotion motion = ToastShowMotion(reduced);
  motion.animate_rise = false;
  motion.rise_px = 0;
  motion.from_opacity = 1.f;
  motion.to_opacity = 0.f;
  motion.easing = MotionEasing::kAccelerate;
  return motion;
}

RenameFadeMotion RenameFade() {
  RenameFadeMotion motion;
  motion.from_opacity = 0.f;
  motion.to_opacity = 1.f;
  motion.duration = MotionDuration::kXfast;
  motion.easing = MotionEasing::kDecelerate;
  motion.duration_ms = GetDurationMs(motion.duration, /*reduced=*/false);
  return motion;
}

RenameFadeMotion RenameFadeOut() {
  RenameFadeMotion motion = RenameFade();
  motion.from_opacity = 1.f;
  motion.to_opacity = 0.f;
  motion.easing = MotionEasing::kAccelerate;
  return motion;
}

FolderMotion FolderRevealMotion(bool reduced) {
  FolderMotion motion;
  motion.duration = MotionDuration::kFolderReveal;
  motion.easing = MotionEasing::kInOut;
  motion.duration_ms = GetDurationMs(motion.duration, reduced);
  motion.from_opacity = 0.f;
  motion.to_opacity = 1.f;
  motion.stagger_ms = reduced ? 0 : aerie::tokens::kMotionTimingStagger;
  motion.stagger_cap = aerie::tokens::kMotionTimingStaggerCap;
  motion.animate_height = !reduced;
  motion.height_duration_ms = reduced ? 0 : motion.duration_ms;
  return motion;
}

FolderMotion FolderCollapseMotion(bool reduced) {
  FolderMotion motion;
  motion.duration = MotionDuration::kFolderCollapse;
  motion.easing = MotionEasing::kInOut;
  motion.duration_ms = GetDurationMs(motion.duration, reduced);
  motion.from_opacity = 1.f;
  motion.to_opacity = 0.f;
  motion.stagger_ms = 0;
  motion.stagger_cap = 0;
  motion.animate_height = !reduced;
  motion.height_duration_ms = reduced ? 0 : motion.duration_ms;
  return motion;
}

ChevronMotion FolderChevronMotion(bool reduced) {
  ChevronMotion motion;
  motion.animate = !reduced;
  motion.open_turn = reduced ? aerie::tokens::kMotionRatioChevronTurnReduced
                             : aerie::tokens::kMotionRatioChevronTurn;
  motion.duration = MotionDuration::kFast;
  motion.easing = MotionEasing::kStandard;
  motion.duration_ms = GetDurationMs(motion.duration, reduced);
  return motion;
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
