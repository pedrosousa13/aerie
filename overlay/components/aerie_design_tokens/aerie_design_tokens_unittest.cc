// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

// Tests of the generated token tables and of the motion helper (issue #174).
// The values written out here are copied from tokens.json on purpose: a token
// edit that changes one must change the test with it. The comparison of every
// table field with the CSS output is tests/test_design_tokens.py, which runs
// in the PR gate.

#include <cmath>
#include <iterator>

#include "base/test/task_environment.h"
#include "components/aerie_design_tokens/aerie_motion.h"
#include "components/aerie_design_tokens/aerie_motion_prefs.h"
#include "components/aerie_design_tokens/aerie_tokens.h"
#include "components/prefs/testing_pref_service.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/compositor/layer_animation_element.h"
#include "ui/compositor/layer_animation_sequence.h"
#include "ui/compositor/layer_animator.h"
#include "ui/compositor/test/layer_animator_test_controller.h"
#include "ui/compositor/test/test_layer_animation_delegate.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/animation/animation_test_api.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/geometry/cubic_bezier.h"
#include "ui/gfx/geometry/transform.h"

namespace aerie {
namespace {

using tokens::ColorMode;
using tokens::MotionDuration;
using tokens::MotionEasing;
using tokens::ThemeId;

// ---- tables ------------------------------------------------------------------------------------------------------

TEST(AerieTokensTest, FlatConstantsHaveTheApprovedValues) {
  EXPECT_EQ(tokens::kSizeSidebarWidth, 260);
  EXPECT_EQ(tokens::kMotionDurationFast, 140);
  EXPECT_EQ(tokens::kMotionDurationFastReduced, 90);
  EXPECT_EQ(tokens::kMotionSpringStiffness, 380);
  EXPECT_EQ(tokens::kMotionSpringDamping, 32);
  EXPECT_EQ(tokens::kMotionSpringMass, 1);
}

TEST(AerieTokensTest, ThereIsATablePerThemeAndSupportedMode) {
  // Six themes, both modes, except OLED which is dark only.
  EXPECT_EQ(std::size(tokens::kThemeTable), 11u);
  EXPECT_EQ(tokens::kThemeCount, 6);
  for (int i = 0; i < tokens::kThemeCount; ++i) {
    const ThemeId theme = static_cast<ThemeId>(i);
    EXPECT_TRUE(tokens::ThemeSupportsMode(theme, ColorMode::kDark)) << i;
    EXPECT_EQ(tokens::ThemeSupportsMode(theme, ColorMode::kLight),
              theme != ThemeId::kOled)
        << i;
  }
}

TEST(AerieTokensTest, AThemeWithoutLightFallsBackToClean) {
  EXPECT_EQ(&tokens::GetThemeTokens(ThemeId::kOled, ColorMode::kLight),
            &tokens::GetThemeTokens(ThemeId::kClean, ColorMode::kLight));
}

TEST(AerieTokensTest, AnOverrideKeyReplacesTheTokenWhileItsThemeIsActive) {
  // color.text of OLED replaces color-text in dark mode.
  EXPECT_EQ(tokens::GetThemeTokens(ThemeId::kOled, ColorMode::kDark).color_text,
            SkColorSetRGB(0xF2, 0xF2, 0xF5));
  // A theme with no color.text key keeps the base token.
  EXPECT_EQ(
      tokens::GetThemeTokens(ThemeId::kVivid, ColorMode::kDark).color_text,
      tokens::kColorTextDark);
  EXPECT_EQ(
      tokens::GetThemeTokens(ThemeId::kVivid, ColorMode::kLight).color_text,
      tokens::kColorTextLight);
  // size.focus-ring-width of High contrast.
  EXPECT_EQ(tokens::GetThemeTokens(ThemeId::kHighContrast, ColorMode::kLight)
                .size_focus_ring_width,
            3);
  EXPECT_EQ(tokens::GetThemeTokens(ThemeId::kVivid, ColorMode::kLight)
                .size_focus_ring_width,
            tokens::kSizeFocusRingWidth);
  // shadow.card of OLED is "none".
  EXPECT_EQ(tokens::GetThemeTokens(ThemeId::kOled, ColorMode::kDark)
                .shadow_card.count,
            0);
  EXPECT_EQ(tokens::GetThemeTokens(ThemeId::kVivid, ColorMode::kDark)
                .shadow_card.count,
            2);
}

TEST(AerieTokensTest, ThemeIdsArePersistedNames) {
  EXPECT_STREQ(tokens::kThemeIds[static_cast<int>(ThemeId::kHighContrast)],
               "high-contrast");
  EXPECT_STREQ(tokens::kThemeIds[static_cast<int>(ThemeId::kOled)], "oled");
}

TEST(AerieTokensTest, WorkspacePresetsHaveStopsAndAnAccent) {
  EXPECT_EQ(tokens::kWorkspacePresetCount, 8);
  const auto& dusk = tokens::kWorkspacePresets[static_cast<int>(
      tokens::WorkspacePresetId::kDusk)];
  EXPECT_EQ(dusk.stop_count, 3);
  EXPECT_EQ(dusk.stops[0], SkColorSetRGB(0xFF, 0xA4, 0x63));
  const auto& tide = tokens::kWorkspacePresets[static_cast<int>(
      tokens::WorkspacePresetId::kTide)];
  EXPECT_EQ(tide.stop_count, 2);
}

TEST(AerieTokensTest, ReducedValuesAreExposed) {
  EXPECT_EQ(tokens::GetDurationMs(MotionDuration::kFast, false), 140);
  EXPECT_EQ(tokens::GetDurationMs(MotionDuration::kFast, true), 90);
  // A token with no reduced value gives its value.
  EXPECT_EQ(tokens::GetDurationMs(MotionDuration::kInstant, true), 0);
  // The spring curve is sampled; its reduced form is a plain bezier.
  const tokens::Easing spring = tokens::GetEasing(MotionEasing::kSpring, false);
  EXPECT_TRUE(spring.sampled);
  EXPECT_EQ(spring.sample_count, 23);
  EXPECT_FLOAT_EQ(spring.samples[0], 0.0f);
  EXPECT_FLOAT_EQ(spring.samples[spring.sample_count - 1], 1.0f);
  EXPECT_FALSE(tokens::GetEasing(MotionEasing::kSpring, true).sampled);
  EXPECT_FALSE(tokens::GetEasing(MotionEasing::kSpringSettle, true).sampled);
}

// ---- reduced motion ----------------------------------------------------------------------------------------------

class ShouldReduceMotionTest : public testing::Test {
 public:
  ShouldReduceMotionTest() { motion::RegisterProfilePrefs(prefs_.registry()); }

 protected:
  TestingPrefServiceSimple prefs_;
};

TEST_F(ShouldReduceMotionTest, DefaultsToTheSystemSetting) {
  EXPECT_EQ(prefs_.GetString(motion::kReduceMotionPref),
            motion::kReduceMotionSystem);
  auto off = gfx::AnimationTestApi::SetPrefersReducedMotionForTesting(false);
  EXPECT_FALSE(motion::ShouldReduceMotion(&prefs_));
  EXPECT_FALSE(motion::ShouldReduceMotion(nullptr));
}

TEST_F(ShouldReduceMotionTest, TheSystemSettingSwitchesItOn) {
  auto on = gfx::AnimationTestApi::SetPrefersReducedMotionForTesting(true);
  EXPECT_TRUE(motion::ShouldReduceMotion(&prefs_));
  EXPECT_TRUE(motion::ShouldReduceMotion(nullptr));
}

TEST_F(ShouldReduceMotionTest, ThePrefSwitchesItOnWithoutTheSystemSetting) {
  auto off = gfx::AnimationTestApi::SetPrefersReducedMotionForTesting(false);
  prefs_.SetString(motion::kReduceMotionPref, motion::kReduceMotionOn);
  EXPECT_TRUE(motion::ShouldReduceMotion(&prefs_));
  prefs_.SetString(motion::kReduceMotionPref, motion::kReduceMotionSystem);
  EXPECT_FALSE(motion::ShouldReduceMotion(&prefs_));
}

// ---- the plan ----------------------------------------------------------------------------------------------------

motion::LayerMotion SlideAndFade() {
  motion::LayerMotion m;
  gfx::Transform t;
  t.Translate(80, 0);
  m.transform = t;
  m.opacity = 0.0f;
  return m;
}

TEST(AerieMotionPlanTest, NormalModeAnimatesTransformAndOpacityOnly) {
  const motion::MotionPlan plan =
      motion::PlanMotion(SlideAndFade(), MotionDuration::kBase,
                         MotionEasing::kStandard, /*reduced=*/false);
  EXPECT_TRUE(plan.animate_transform);
  EXPECT_TRUE(plan.animate_opacity);
  EXPECT_FALSE(plan.set_transform_now);
  EXPECT_EQ(plan.duration, base::Milliseconds(200));
}

TEST(AerieMotionPlanTest, ReducedModeCreatesNoTransformElement) {
  const motion::MotionPlan plan =
      motion::PlanMotion(SlideAndFade(), MotionDuration::kBase,
                         MotionEasing::kStandard, /*reduced=*/true);
  EXPECT_FALSE(plan.animate_transform);
  EXPECT_TRUE(plan.set_transform_now);  // the instant set
  EXPECT_TRUE(plan.animate_opacity);
  EXPECT_EQ(plan.duration, base::Milliseconds(140));  // base, reduced
}

TEST(AerieMotionPlanTest, NoOpacityElementIsLongerThanTheReducedCap) {
  const MotionDuration all[] = {
      MotionDuration::kXfast,           MotionDuration::kFast,
      MotionDuration::kBase,            MotionDuration::kSlow,
      MotionDuration::kXslow,           MotionDuration::kPeekShow,
      MotionDuration::kPeekHide,        MotionDuration::kFolderCollapse,
      MotionDuration::kFolderReveal,    MotionDuration::kWorkspaceSwitch,
      MotionDuration::kSheetEnter,      MotionDuration::kSheetExit,
      MotionDuration::kSwitch,          MotionDuration::kAutofilePulse,
      MotionDuration::kArchiveCollapse, MotionDuration::kArchivePulse,
      MotionDuration::kArchiveRestore,  MotionDuration::kTabOpen,
      MotionDuration::kTabClose,
  };
  motion::LayerMotion fade;
  fade.opacity = 0.5f;
  for (MotionDuration d : all) {
    const motion::MotionPlan plan =
        motion::PlanMotion(fade, d, MotionEasing::kSpring, /*reduced=*/true);
    EXPECT_LE(plan.duration, base::Milliseconds(motion::kMaxReducedOpacityMs));
    EXPECT_FALSE(plan.animate_transform);
  }
}

TEST(AerieMotionPlanTest, ReducedModeWithOnlyATransformSetsItAtOnce) {
  motion::LayerMotion slide;
  slide.transform = gfx::Transform();
  const motion::MotionPlan plan = motion::PlanMotion(
      slide, MotionDuration::kBase, MotionEasing::kStandard, true);
  EXPECT_TRUE(plan.set_transform_now);
  EXPECT_FALSE(plan.animate_transform);
  EXPECT_FALSE(plan.animate_opacity);
}

TEST(AerieMotionPlanTest, AZeroDurationSetsTheTargetsAtOnce) {
  const motion::MotionPlan plan =
      motion::PlanMotion(SlideAndFade(), MotionDuration::kInstant,
                         MotionEasing::kStandard, /*reduced=*/false);
  EXPECT_TRUE(plan.set_transform_now);
  EXPECT_TRUE(plan.set_opacity_now);
  EXPECT_FALSE(plan.animate_transform);
  EXPECT_FALSE(plan.animate_opacity);
}

TEST(AerieMotionPlanTest, EachBezierTokenRunsAsOneExactTweenType) {
  struct Case {
    MotionEasing token;
    gfx::Tween::Type type;
    tokens::CubicBezier curve;
  } const cases[] = {
      {MotionEasing::kStandard, gfx::Tween::FAST_OUT_SLOW_IN_3,
       tokens::kMotionEasingStandard},
      {MotionEasing::kDecelerate, gfx::Tween::ACCEL_5_70_DECEL_90,
       tokens::kMotionEasingDecelerate},
      {MotionEasing::kAccelerate, gfx::Tween::ACCEL_30_DECEL_20_85,
       tokens::kMotionEasingAccelerate},
      {MotionEasing::kInOut, gfx::Tween::FAST_OUT_SLOW_IN,
       tokens::kMotionEasingInOut},
      {MotionEasing::kLinear, gfx::Tween::LINEAR, tokens::kMotionEasingLinear},
  };
  for (const Case& c : cases) {
    const motion::MotionPlan plan = motion::PlanMotion(
        SlideAndFade(), MotionDuration::kBase, c.token, false);
    EXPECT_TRUE(plan.segments.empty());
    EXPECT_EQ(plan.tween, c.type);
    // The Tween type is the token's curve, not a near match.
    const gfx::CubicBezier bezier(c.curve.x1, c.curve.y1, c.curve.x2,
                                  c.curve.y2);
    for (double t = 0.05; t < 1.0; t += 0.05) {
      EXPECT_NEAR(gfx::Tween::CalculateValue(c.type, t), bezier.Solve(t), 1e-4);
    }
  }
}

TEST(AerieMotionPlanTest, ASpringIsChainedLinearSegmentsOfItsSamples) {
  const motion::MotionPlan plan =
      motion::PlanMotion(SlideAndFade(), MotionDuration::kWorkspaceSwitch,
                         MotionEasing::kSpring, false);
  const tokens::Easing spring = tokens::GetEasing(MotionEasing::kSpring, false);
  ASSERT_EQ(plan.segments.size(), static_cast<size_t>(spring.sample_count - 1));
  base::TimeDelta total;
  for (size_t i = 0; i < plan.segments.size(); ++i) {
    EXPECT_DOUBLE_EQ(plan.segments[i].progress, spring.samples[i + 1]);
    EXPECT_GT(plan.segments[i].duration, base::TimeDelta());
    total += plan.segments[i].duration;
  }
  EXPECT_EQ(total, plan.duration);
  EXPECT_DOUBLE_EQ(plan.segments.back().progress, 1.0);
}

// ---- the sequences, run by the animator --------------------------------------------------------------------------

class AerieMotionAnimationTest : public testing::Test {
 public:
  AerieMotionAnimationTest()
      : animator_(ui::LayerAnimator::CreateDefaultAnimator()) {
    animator_->set_disable_timer_for_test(true);
    animator_->SetDelegate(&delegate_);
  }
  ~AerieMotionAnimationTest() override { animator_->SetDelegate(nullptr); }

  motion::MotionHandle Start(const motion::LayerMotion& m,
                             MotionDuration duration,
                             MotionEasing easing,
                             bool reduced) {
    const motion::MotionPlan plan =
        motion::PlanMotion(m, duration, easing, reduced);
    return motion::StartMotion(animator_.get(), plan, m,
                               delegate_.GetTransformForAnimation(),
                               delegate_.GetOpacityForAnimation());
  }

  float Opacity() { return delegate_.GetOpacityForAnimation(); }
  float TranslationX() {
    return delegate_.GetTransformForAnimation().To2dTranslation().x();
  }

 protected:
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::MainThreadType::UI};
  ui::TestLayerAnimationDelegate delegate_;
  scoped_refptr<ui::LayerAnimator> animator_;
};

// Transform and opacity animations are threaded: the compositor runs them and
// the delegate only gets the target when they end. So these tests check that
// the sequences run, and the values the delegate holds after they end. They
// step past the end (a step to exactly the duration can fall short by the few
// microseconds between the animator's start time and the test controller's).

TEST_F(AerieMotionAnimationTest, NormalModeRunsTransformAndOpacity) {
  ui::LayerAnimatorTestController controller(animator_);
  Start(SlideAndFade(), MotionDuration::kBase, MotionEasing::kLinear, false);
  controller.StartThreadedAnimationsIfNeeded();
  EXPECT_NE(controller.GetRunningSequence(ui::LayerAnimationElement::TRANSFORM),
            nullptr);
  EXPECT_NE(controller.GetRunningSequence(ui::LayerAnimationElement::OPACITY),
            nullptr);
  EXPECT_TRUE(animator_->is_animating());
  controller.Step(base::Milliseconds(250));
  EXPECT_FLOAT_EQ(TranslationX(), 80.0f);
  EXPECT_FLOAT_EQ(Opacity(), 0.0f);
  EXPECT_FALSE(animator_->is_animating());
}

TEST_F(AerieMotionAnimationTest, ReducedModeSetsTheTransformAndFadesTheOpacity) {
  ui::LayerAnimatorTestController controller(animator_);
  Start(SlideAndFade(), MotionDuration::kBase, MotionEasing::kStandard, true);
  controller.StartThreadedAnimationsIfNeeded();
  // No transform element ran: the value is already the target.
  EXPECT_EQ(controller.GetRunningSequence(ui::LayerAnimationElement::TRANSFORM),
            nullptr);
  EXPECT_FLOAT_EQ(TranslationX(), 80.0f);
  EXPECT_NE(controller.GetRunningSequence(ui::LayerAnimationElement::OPACITY),
            nullptr);
  controller.Step(base::Milliseconds(motion::kMaxReducedOpacityMs + 50));
  EXPECT_FLOAT_EQ(Opacity(), 0.0f);
  EXPECT_FALSE(animator_->is_animating());
}

TEST_F(AerieMotionAnimationTest, ANewAnimationReplacesTheRunningOne) {
  ui::LayerAnimatorTestController controller(animator_);
  Start(SlideAndFade(), MotionDuration::kBase, MotionEasing::kLinear, false);
  controller.StartThreadedAnimationsIfNeeded();
  controller.Step(base::Milliseconds(100));

  motion::LayerMotion back;
  back.transform = gfx::Transform();
  Start(back, MotionDuration::kBase, MotionEasing::kLinear, false);
  controller.StartThreadedAnimationsIfNeeded();
  // One owner: the first transform sequence is gone, only the new one runs.
  EXPECT_NE(controller.GetRunningSequence(ui::LayerAnimationElement::TRANSFORM),
            nullptr);
  EXPECT_EQ(controller.GetRunningSequence(ui::LayerAnimationElement::OPACITY),
            nullptr);
  controller.Step(base::Milliseconds(250));
  EXPECT_FLOAT_EQ(TranslationX(), 0.0f);
  EXPECT_FALSE(animator_->is_animating());
}

TEST_F(AerieMotionAnimationTest, TheHandleAbortsAndKeepsTheReachedValue) {
  ui::LayerAnimatorTestController controller(animator_);
  motion::MotionHandle handle = Start(SlideAndFade(), MotionDuration::kBase,
                                      MotionEasing::kLinear, false);
  controller.StartThreadedAnimationsIfNeeded();
  controller.Step(base::Milliseconds(100));
  EXPECT_TRUE(handle.IsAnimating());
  handle.Abort();
  EXPECT_FALSE(handle.IsAnimating());
  const float reached = TranslationX();
  controller.Step(base::Milliseconds(100));
  EXPECT_FLOAT_EQ(TranslationX(), reached);
}

TEST_F(AerieMotionAnimationTest, ASpringEndsOnTheTarget) {
  ui::LayerAnimatorTestController controller(animator_);
  Start(SlideAndFade(), MotionDuration::kWorkspaceSwitch,
        MotionEasing::kSpringSettle, false);
  controller.StartThreadedAnimationsIfNeeded();
  controller.Step(base::Seconds(1));
  EXPECT_FLOAT_EQ(TranslationX(), 80.0f);
  EXPECT_FALSE(animator_->is_animating());
}

}  // namespace
}  // namespace aerie
