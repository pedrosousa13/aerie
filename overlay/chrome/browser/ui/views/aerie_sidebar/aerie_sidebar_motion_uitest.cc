// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

// The motion of the sidebar (issue #215, items 4 and 5; docs/sidebar-design.md,
// "Performance rules" 1, 2 and 6): the tests read the layer animators and
// assert that only transform and opacity animate, that the page card follows
// the sidebar with a transform while its bounds change in one step, and that
// under reduced motion no transform animates and every opacity animation lasts
// 140 ms or less. Run with ./aerie test-cpp --target aerie_interactive_ui_tests.

#include <cmath>
#include <optional>

#include "base/test/run_until.h"
#include "base/time/time.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_row_view.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_view.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/multi_contents_view.h"
#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/interactive_test_utils.h"
#include "components/aerie_sidebar/aerie_sidebar_layout.h"
#include "components/aerie_sidebar/aerie_sidebar_prefs.h"
#include "components/prefs/pref_service.h"
#include "content/public/test/browser_test.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/layer_animation_element.h"
#include "ui/compositor/layer_animation_sequence.h"
#include "ui/compositor/layer_animator.h"
#include "ui/compositor/test/layer_animator_test_controller.h"
#include "ui/gfx/animation/animation_test_api.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"

namespace {

using Property = ui::LayerAnimationElement::AnimatableProperty;

template <typename T>
T* FindDescendant(views::View* root) {
  if (auto* match = views::AsViewClass<T>(root)) {
    return match;
  }
  for (views::View* child : root->children()) {
    if (T* found = FindDescendant<T>(child)) {
      return found;
    }
  }
  return nullptr;
}

bool Animates(ui::Layer* layer, Property property) {
  return layer->GetAnimator()->IsAnimatingProperty(property);
}

// True when nothing but transform and opacity animates on `layer`.
bool OnlyTransformAndOpacityAnimate(ui::Layer* layer) {
  for (int bit = ui::LayerAnimationElement::FIRST_PROPERTY;
       bit != ui::LayerAnimationElement::SENTINEL; bit <<= 1) {
    const auto property = static_cast<Property>(bit);
    if (property == ui::LayerAnimationElement::TRANSFORM ||
        property == ui::LayerAnimationElement::OPACITY) {
      continue;
    }
    if (Animates(layer, property)) {
      ADD_FAILURE() << "property " << bit << " animates";
      return false;
    }
  }
  return true;
}

// How long the running opacity animation of `layer` takes.
base::TimeDelta OpacityDuration(ui::Layer* layer) {
  ui::LayerAnimatorTestController controller(layer->GetAnimator());
  ui::LayerAnimationSequence* sequence =
      controller.GetRunningSequence(ui::LayerAnimationElement::OPACITY);
  // The opacity of a token that has a Tween type is one element.
  EXPECT_LE(sequence ? sequence->size() : 1u, 1u);
  return sequence ? sequence->FirstElement()->duration() : base::TimeDelta();
}

}  // namespace

class AerieSidebarMotionUiTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));
    ASSERT_TRUE(base::test::RunUntil([&] { return sidebar() && sidebar()->row_count() == 1u; }));
  }

  void ForceReducedMotion(bool reduced) {
    reduced_motion_.reset();
    reduced_motion_.emplace(
        gfx::AnimationTestApi::SetPrefersReducedMotionForTesting(reduced));
  }

  BrowserView* browser_view() {
    return BrowserView::GetBrowserViewForBrowser(browser());
  }
  VerticalTabStripRegionView* region() {
    return browser_view()->vertical_tab_strip_region_view_for_testing();
  }
  AerieSidebarView* sidebar() { return FindDescendant<AerieSidebarView>(region()); }
  views::View* card() { return browser_view()->multi_contents_view(); }
  PrefService* prefs() { return browser()->GetProfile()->GetPrefs(); }

  AerieSidebarRowView* OpenTab() {
    const size_t before = sidebar()->row_count();
    chrome::ExecuteCommand(browser(), IDC_NEW_TAB);
    EXPECT_TRUE(base::test::RunUntil([&] { return sidebar()->row_count() == before + 1; }));
    return sidebar()->row_at(before);
  }

  // Closes the last tab and returns the row that is now closing.
  AerieSidebarRowView* CloseLastTab() {
    TabStripModel* tabs = browser()->tab_strip_model();
    tabs->CloseWebContentsAt(tabs->count() - 1, TabCloseTypes::CLOSE_NONE);
    EXPECT_TRUE(base::test::RunUntil([&] {
      return sidebar()->closing_row_count() == 1u;
    }));
    return sidebar()->closing_row_at(0);
  }

 private:
  std::optional<gfx::AnimationTestApi::PrefersReducedMotionResetter>
      reduced_motion_;
};

IN_PROC_BROWSER_TEST_F(AerieSidebarMotionUiTest, OpeningARowAnimatesOnlyTransformAndOpacityAndTheHeightGrows) {
  ForceReducedMotion(false);
  AerieSidebarRowView* row = OpenTab();
  ASSERT_TRUE(row);
  ASSERT_TRUE(row->content()->layer());
  ui::Layer* layer = row->content()->layer();

  EXPECT_TRUE(Animates(layer, ui::LayerAnimationElement::TRANSFORM));
  EXPECT_TRUE(Animates(layer, ui::LayerAnimationElement::OPACITY));
  EXPECT_TRUE(OnlyTransformAndOpacityAnimate(layer));
  // The row content scales in (0.96 to 1) and fades in: `fast`, 140 ms.
  EXPECT_EQ(OpacityDuration(layer), base::Milliseconds(140));
  EXPECT_TRUE(layer->GetTargetTransform().IsIdentity());
  EXPECT_EQ(layer->GetTargetOpacity(), 1.f);
  // The row height is the one property that lays out.
  EXPECT_TRUE(row->height_animating());

  // At rest the row has no height animation and a full height.
  ASSERT_TRUE(base::test::RunUntil([&] { return !row->height_animating(); }));
  EXPECT_EQ(row->GetPreferredSize().height(), 34);
}

IN_PROC_BROWSER_TEST_F(AerieSidebarMotionUiTest, ClosingARowSlidesAndFadesThenTheHeightCollapses) {
  ForceReducedMotion(false);
  OpenTab();
  AerieSidebarRowView* row = CloseLastTab();
  ASSERT_TRUE(row);
  ASSERT_TRUE(row->is_closing());
  ASSERT_TRUE(row->content()->layer());
  ui::Layer* layer = row->content()->layer();

  EXPECT_TRUE(Animates(layer, ui::LayerAnimationElement::TRANSFORM));
  EXPECT_TRUE(Animates(layer, ui::LayerAnimationElement::OPACITY));
  EXPECT_TRUE(OnlyTransformAndOpacityAnimate(layer));
  // It slides 8 px toward the outer edge and fades out.
  EXPECT_EQ(std::abs(layer->GetTargetTransform().To2dTranslation().x()), 8.f);
  EXPECT_EQ(layer->GetTargetOpacity(), 0.f);
  EXPECT_EQ(OpacityDuration(layer), base::Milliseconds(140));

  // Then the height collapses, and the row goes.
  EXPECT_TRUE(base::test::RunUntil([&] { return row->height_animating(); }) ||
              sidebar()->closing_row_count() == 0u);
  ASSERT_TRUE(base::test::RunUntil([&] { return sidebar()->closing_row_count() == 0u; }));
  // The list holds the rows that are open and nothing else.
  size_t rows_in_list = 0;
  for (views::View* child : sidebar()->today_tree()->children()) {
    rows_in_list += views::IsViewClass<AerieSidebarRowView>(child) ? 1 : 0;
  }
  EXPECT_EQ(rows_in_list, sidebar()->row_count());
}

IN_PROC_BROWSER_TEST_F(AerieSidebarMotionUiTest, ReducedMotionOpensAndClosesWithoutATransformAndFadesInUnder140ms) {
  ForceReducedMotion(true);
  AerieSidebarRowView* row = OpenTab();
  ASSERT_TRUE(row);
  ASSERT_TRUE(row->content()->layer());
  ui::Layer* layer = row->content()->layer();
  EXPECT_FALSE(Animates(layer, ui::LayerAnimationElement::TRANSFORM));
  EXPECT_TRUE(layer->GetTargetTransform().IsIdentity());
  EXPECT_TRUE(layer->transform().IsIdentity());
  EXPECT_TRUE(OnlyTransformAndOpacityAnimate(layer));
  EXPECT_TRUE(Animates(layer, ui::LayerAnimationElement::OPACITY));
  EXPECT_LE(OpacityDuration(layer), base::Milliseconds(140));
  // The height snaps.
  EXPECT_FALSE(row->height_animating());
  EXPECT_EQ(row->GetPreferredSize().height(), 34);

  AerieSidebarRowView* closing = CloseLastTab();
  ASSERT_TRUE(closing);
  ASSERT_TRUE(closing->content()->layer());
  ui::Layer* closing_layer = closing->content()->layer();
  EXPECT_FALSE(Animates(closing_layer, ui::LayerAnimationElement::TRANSFORM));
  EXPECT_TRUE(closing_layer->GetTargetTransform().IsIdentity());
  EXPECT_TRUE(OnlyTransformAndOpacityAnimate(closing_layer));
  EXPECT_LE(OpacityDuration(closing_layer), base::Milliseconds(140));
  EXPECT_FALSE(closing->height_animating());
  ASSERT_TRUE(base::test::RunUntil([&] { return sidebar()->closing_row_count() == 0u; }));
}

IN_PROC_BROWSER_TEST_F(AerieSidebarMotionUiTest, HideAndShowAnimateOnlyTransformAndTheCardFollowsInOneStep) {
  ForceReducedMotion(false);
  ASSERT_TRUE(base::test::RunUntil([&] { return region()->width() > 0; }));
  const int column_width = region()->width();
  const gfx::Rect card_before = card()->bounds();
  ui::Layer* column = region()->layer();

  // Hide (Ctrl+S): the column slides out by a transform, 200 ms.
  chrome::ExecuteCommand(browser(), IDC_CTRL_S_SHORTCUT);
  EXPECT_TRUE(Animates(column, ui::LayerAnimationElement::TRANSFORM));
  EXPECT_FALSE(Animates(column, ui::LayerAnimationElement::OPACITY));
  EXPECT_TRUE(OnlyTransformAndOpacityAnimate(column));

  // The card has its new bounds in one step and follows with a transform: it
  // is wider by the column, and starts where it was.
  ASSERT_TRUE(base::test::RunUntil([&] { return card()->width() > card_before.width(); }));
  const gfx::Rect card_after = card()->bounds();
  EXPECT_GE(card_after.width() - card_before.width(), column_width - 2);
  ASSERT_TRUE(base::test::RunUntil([&] { return card()->layer() != nullptr; }));
  ui::Layer* card_layer = card()->layer();
  EXPECT_TRUE(Animates(card_layer, ui::LayerAnimationElement::TRANSFORM));
  EXPECT_FALSE(Animates(card_layer, ui::LayerAnimationElement::OPACITY));
  EXPECT_TRUE(OnlyTransformAndOpacityAnimate(card_layer));
  EXPECT_TRUE(card_layer->GetTargetTransform().IsIdentity());
  EXPECT_NE(card_layer->transform().To2dTranslation().x(), 0.f);

  // Nothing lays out per frame: the card's bounds stay what they are for the
  // whole animation.
  ASSERT_TRUE(base::test::RunUntil([&] { return region()->aerie_sidebar_hidden(); }));
  EXPECT_EQ(card()->bounds(), card_after);

  // Show: the card is narrower at once and follows the column back.
  chrome::ExecuteCommand(browser(), IDC_CTRL_S_SHORTCUT);
  EXPECT_TRUE(Animates(region()->layer(), ui::LayerAnimationElement::TRANSFORM));
  EXPECT_TRUE(OnlyTransformAndOpacityAnimate(region()->layer()));
  ASSERT_TRUE(base::test::RunUntil([&] { return card()->width() < card_after.width(); }));
  const gfx::Rect card_shown = card()->bounds();
  ASSERT_TRUE(base::test::RunUntil([&] { return card()->layer() != nullptr; }));
  EXPECT_TRUE(Animates(card()->layer(), ui::LayerAnimationElement::TRANSFORM));
  EXPECT_TRUE(OnlyTransformAndOpacityAnimate(card()->layer()));
  const base::TimeTicks until = base::TimeTicks::Now() + base::Milliseconds(400);
  base::test::RunUntil([&] { return base::TimeTicks::Now() >= until; });
  EXPECT_EQ(card()->bounds(), card_shown);
}

IN_PROC_BROWSER_TEST_F(AerieSidebarMotionUiTest, ReducedMotionFadesTheColumnAndTheCardSnaps) {
  ForceReducedMotion(true);
  ASSERT_TRUE(base::test::RunUntil([&] { return region()->width() > 0; }));
  const gfx::Rect card_before = card()->bounds();
  ui::Layer* column = region()->layer();

  chrome::ExecuteCommand(browser(), IDC_CTRL_S_SHORTCUT);
  // The column fades (opacity, at most 140 ms); no transform is created.
  EXPECT_FALSE(Animates(column, ui::LayerAnimationElement::TRANSFORM));
  EXPECT_TRUE(Animates(column, ui::LayerAnimationElement::OPACITY));
  EXPECT_TRUE(OnlyTransformAndOpacityAnimate(column));
  EXPECT_LE(OpacityDuration(column), base::Milliseconds(140));
  EXPECT_TRUE(column->GetTargetTransform().IsIdentity());

  // The card snaps: new bounds, no animation on its layer.
  ASSERT_TRUE(base::test::RunUntil([&] { return card()->width() > card_before.width(); }));
  if (card()->layer()) {
    EXPECT_FALSE(card()->layer()->GetAnimator()->is_animating());
    EXPECT_TRUE(card()->layer()->transform().IsIdentity());
  }

  ASSERT_TRUE(base::test::RunUntil([&] { return region()->aerie_sidebar_hidden(); }));
  chrome::ExecuteCommand(browser(), IDC_CTRL_S_SHORTCUT);
  EXPECT_FALSE(Animates(region()->layer(), ui::LayerAnimationElement::TRANSFORM));
  EXPECT_LE(OpacityDuration(region()->layer()), base::Milliseconds(140));
  ASSERT_TRUE(base::test::RunUntil([&] { return card()->width() < card_before.width() + 1; }));
  if (card()->layer()) {
    EXPECT_FALSE(card()->layer()->GetAnimator()->is_animating());
  }
}
