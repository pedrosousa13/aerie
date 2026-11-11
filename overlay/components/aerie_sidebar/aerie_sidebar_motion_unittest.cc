// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_sidebar/aerie_sidebar_motion.h"

#include "components/aerie_design_tokens/aerie_motion.h"
#include "components/aerie_design_tokens/aerie_tokens.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace aerie_sidebar {
namespace {

using aerie::tokens::MotionDuration;
using aerie::tokens::MotionEasing;

TEST(AerieSidebarMotionTest, ShowIsSlowOnDecelerateAndSlidesPastItsWidth) {
  const SlideMotion m = ShowMotion(260, -1, /*reduced=*/false);
  EXPECT_EQ(m.duration, MotionDuration::kSlow);
  EXPECT_EQ(m.easing, MotionEasing::kDecelerate);
  EXPECT_EQ(m.duration_ms, 280);
  EXPECT_TRUE(m.animate_transform);
  EXPECT_FALSE(m.animate_opacity);
  EXPECT_EQ(m.hidden_x, -(260 + 24));
}

TEST(AerieSidebarMotionTest, HideIsBaseOnAccelerate) {
  const SlideMotion m = HideMotion(300, 1, /*reduced=*/false);
  EXPECT_EQ(m.duration, MotionDuration::kBase);
  EXPECT_EQ(m.easing, MotionEasing::kAccelerate);
  EXPECT_EQ(m.duration_ms, 200);
  EXPECT_TRUE(m.animate_transform);
  EXPECT_EQ(m.hidden_x, 300 + 24);
}

TEST(AerieSidebarMotionTest, ReducedShowAndHideFadeAndNeverMove) {
  const SlideMotion show = ShowMotion(260, -1, /*reduced=*/true);
  const SlideMotion hide = HideMotion(260, -1, /*reduced=*/true);
  for (const SlideMotion& m : {show, hide}) {
    EXPECT_FALSE(m.animate_transform);
    EXPECT_TRUE(m.animate_opacity);
    EXPECT_LE(m.duration_ms, 140);
  }
  EXPECT_EQ(show.from_opacity, 0.f);
  EXPECT_EQ(show.to_opacity, 1.f);
  EXPECT_EQ(hide.from_opacity, 1.f);
  EXPECT_EQ(hide.to_opacity, 0.f);
}

TEST(AerieSidebarMotionTest, DragLiftsToTheTokenShiftsOnBaseAndSettlesOnTheSpring) {
  const DragMotion m = RowDragMotion(/*reduced=*/false);
  EXPECT_TRUE(m.animate_lift);
  EXPECT_EQ(m.lift_scale, aerie::tokens::kMotionScaleLift);
  EXPECT_EQ(m.lift_scale, 1.02f);
  EXPECT_EQ(m.lift_duration, MotionDuration::kFast);
  EXPECT_EQ(m.lift_easing, MotionEasing::kStandard);
  EXPECT_TRUE(m.animate_shift);
  EXPECT_EQ(m.shift_duration, MotionDuration::kBase);
  EXPECT_EQ(m.shift_easing, MotionEasing::kStandard);
  EXPECT_TRUE(m.animate_settle);
  EXPECT_EQ(m.settle_duration_ms, 420);
  EXPECT_EQ(m.settle_reduced_duration_ms, 140);
  EXPECT_EQ(m.settle_easing, MotionEasing::kSpring);
}

TEST(AerieSidebarMotionTest, ReducedDragHasNoLiftNoShiftAndAnInstantDrop) {
  const DragMotion m = RowDragMotion(/*reduced=*/true);
  EXPECT_FALSE(m.animate_lift);
  EXPECT_FALSE(m.animate_shift);
  EXPECT_FALSE(m.animate_settle);
}

TEST(AerieSidebarMotionTest, OpenRowScalesFromTheTokenFadesInAndGrowsTheHeight) {
  const RowMotion m = OpenRowMotion(/*reduced=*/false);
  EXPECT_TRUE(m.animate_scale);
  EXPECT_EQ(m.from_scale, aerie::tokens::kMotionScaleEnter);
  EXPECT_FALSE(m.animate_slide);
  EXPECT_EQ(m.from_opacity, 0.f);
  EXPECT_EQ(m.to_opacity, 1.f);
  EXPECT_EQ(m.content_duration, MotionDuration::kFast);
  EXPECT_EQ(m.content_easing, MotionEasing::kDecelerate);
  EXPECT_TRUE(m.animate_height);
  EXPECT_EQ(m.height_duration_ms, 200);
  EXPECT_EQ(m.height_easing, MotionEasing::kStandard);
}

TEST(AerieSidebarMotionTest, CloseRowSlidesTowardTheOuterEdgeThenCollapses) {
  const RowMotion left = CloseRowMotion(-1, /*reduced=*/false);
  const RowMotion right = CloseRowMotion(1, /*reduced=*/false);
  EXPECT_TRUE(left.animate_slide);
  EXPECT_EQ(left.slide_x, -8);
  EXPECT_EQ(right.slide_x, 8);
  EXPECT_FALSE(left.animate_scale);
  EXPECT_EQ(left.from_opacity, 1.f);
  EXPECT_EQ(left.to_opacity, 0.f);
  EXPECT_EQ(left.content_easing, MotionEasing::kAccelerate);
  EXPECT_TRUE(left.animate_height);
  EXPECT_EQ(left.height_duration_ms, 200);
}

TEST(AerieSidebarMotionTest, ReducedRowMotionCreatesNoTransformAndSnapsTheHeight) {
  for (const RowMotion& m :
       {OpenRowMotion(/*reduced=*/true), CloseRowMotion(-1, /*reduced=*/true)}) {
    EXPECT_FALSE(m.animate_scale);
    EXPECT_FALSE(m.animate_slide);
    EXPECT_EQ(m.slide_x, 0);
    EXPECT_FALSE(m.animate_height);
    EXPECT_EQ(m.height_duration_ms, 0);
    EXPECT_LE(m.content_duration_ms, 140);
  }
}

TEST(AerieSidebarMotionTest, AHidingColumnKeepsItsWidthOutsideItsZeroWidthRegion) {
  HideSlide slide;
  EXPECT_FALSE(slide.active());
  slide.Begin(260);
  EXPECT_TRUE(slide.active());
  EXPECT_EQ(slide.content_width(), 260);
  EXPECT_EQ(slide.content_x(/*right_aligned=*/false), 0);
  EXPECT_EQ(slide.content_x(/*right_aligned=*/true), -260);
  slide.End();
  EXPECT_FALSE(slide.active());
}

}  // namespace
TEST(AerieSidebarMotionTest,
     ASleepingFaviconFadesToTheTokenOnFastStandardThenDesaturatesStatically) {
  const SleepMotion m = SleepFaviconMotion(/*reduced=*/false);
  EXPECT_TRUE(m.animate_fade);
  EXPECT_EQ(m.duration, MotionDuration::kFast);
  EXPECT_EQ(m.easing, MotionEasing::kStandard);
  EXPECT_FLOAT_EQ(m.asleep_opacity, 0.55f);
  EXPECT_EQ(m.asleep_opacity, aerie::tokens::kMotionOpacitySleeping);
  // The desaturate is a static filter that follows the fade; it is never
  // animated.
}

TEST(AerieSidebarMotionTest, ReducedMotionSwapsTheSleepingFaviconAtOnce) {
  const SleepMotion m = SleepFaviconMotion(/*reduced=*/true);
  EXPECT_FALSE(m.animate_fade);
  EXPECT_FLOAT_EQ(m.asleep_opacity, 0.55f);
}

TEST(AerieSidebarMotionTest, PinAndUnpinMoveTheTabOnTheSpringAndTheOthersOnBaseStandard) {
  const PinMotion m = PinFlipMotion(/*reduced=*/false);
  EXPECT_TRUE(m.animate);
  EXPECT_EQ(m.moved_duration_ms, 420);
  EXPECT_EQ(m.moved_duration_ms, aerie::tokens::kMotionSpringDuration);
  EXPECT_EQ(m.moved_easing, MotionEasing::kSpring);
  EXPECT_EQ(m.others_duration, MotionDuration::kBase);
  EXPECT_EQ(m.others_easing, MotionEasing::kStandard);
}

TEST(AerieSidebarMotionTest, ReducedMotionPinHasNoTransformAnimationAndAShortSpring) {
  const PinMotion m = PinFlipMotion(/*reduced=*/true);
  EXPECT_FALSE(m.animate);
  EXPECT_LE(m.moved_reduced_duration_ms, aerie::motion::kMaxReducedOpacityMs);
}

TEST(AerieSidebarMotionTest, TheContextMenuScalesFromNinetySevenAndFadesInOnFastDecelerate) {
  const MenuOpenMotion m = ContextMenuOpenMotion(/*reduced=*/false);
  EXPECT_TRUE(m.animate_scale);
  EXPECT_FLOAT_EQ(m.from_scale, 0.97f);
  EXPECT_FLOAT_EQ(m.from_opacity, 0.f);
  EXPECT_EQ(m.duration, MotionDuration::kFast);
  EXPECT_EQ(m.easing, MotionEasing::kDecelerate);
  EXPECT_EQ(m.duration_ms, 140);
}

TEST(AerieSidebarMotionTest, TheContextMenuOnlyFadesInUnderReducedMotion) {
  const MenuOpenMotion m = ContextMenuOpenMotion(/*reduced=*/true);
  EXPECT_FALSE(m.animate_scale);
  EXPECT_EQ(m.duration_ms, 90);
  EXPECT_LE(m.duration_ms, aerie::motion::kMaxReducedOpacityMs);
}

TEST(AerieSidebarMotionTest, TheToastRisesSixPixelsAndFadesInOnFastDecelerateAndHoldsForTheActionTiming) {
  const ToastMotion m = ToastShowMotion(/*reduced=*/false);
  EXPECT_TRUE(m.animate_rise);
  EXPECT_EQ(m.rise_px, 6);
  EXPECT_FLOAT_EQ(m.from_opacity, 0.f);
  EXPECT_FLOAT_EQ(m.to_opacity, 1.f);
  EXPECT_EQ(m.duration, MotionDuration::kFast);
  EXPECT_EQ(m.easing, MotionEasing::kDecelerate);
  EXPECT_EQ(m.duration_ms, 140);
  EXPECT_EQ(m.hold_ms, aerie::tokens::kMotionTimingToastAction);
}

TEST(AerieSidebarMotionTest, TheToastFadesOutOnFastAccelerateWithoutMoving) {
  const ToastMotion m = ToastHideMotion(/*reduced=*/false);
  EXPECT_FALSE(m.animate_rise);
  EXPECT_FLOAT_EQ(m.from_opacity, 1.f);
  EXPECT_FLOAT_EQ(m.to_opacity, 0.f);
  EXPECT_EQ(m.duration, MotionDuration::kFast);
  EXPECT_EQ(m.easing, MotionEasing::kAccelerate);
}

TEST(AerieSidebarMotionTest, UnderReducedMotionTheToastOnlyFadesAndNeverLongerThanTheLimit) {
  for (const ToastMotion& m : {ToastShowMotion(true), ToastHideMotion(true)}) {
    EXPECT_FALSE(m.animate_rise);
    EXPECT_EQ(m.rise_px, 0);
    EXPECT_LE(m.duration_ms, aerie::motion::kMaxReducedOpacityMs);
  }
}

}  // namespace aerie_sidebar
