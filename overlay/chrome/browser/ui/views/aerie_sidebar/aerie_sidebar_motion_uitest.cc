// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

// The motion of the sidebar (issue #215, items 4 and 5; docs/sidebar-design.md,
// "Performance rules" 1, 2 and 6): the tests read the layer animators and
// assert that only transform and opacity animate, that the page card follows
// the sidebar with a transform while its bounds change in one step, and that
// under reduced motion no transform animates and every opacity animation lasts
// 140 ms or less. Run with ./aerie test-cpp --target aerie_interactive_ui_tests.

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/run_loop.h"
#include "base/scoped_observation.h"
#include "base/task/single_thread_task_runner.h"
#include "base/test/run_until.h"
#include "base/time/time.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/aerie_tab_model/tab_model_service.h"
#include "chrome/browser/aerie_tab_model/tab_model_service_factory.h"
#include "chrome/browser/aerie_tab_model/tab_strip_adapter.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_floating.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_folder_group.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_folder_row_view.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_peek_surface.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_row_view.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_view.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/contents_web_view.h"
#include "chrome/browser/ui/views/frame/multi_contents_view.h"
#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/interactive_test_utils.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/aerie_design_tokens/aerie_motion_prefs.h"
#include "components/aerie_sidebar/aerie_sidebar_layout.h"
#include "components/aerie_sidebar/aerie_sidebar_prefs.h"
#include "components/aerie_tab_model/folder_rules.h"
#include "components/aerie_tab_model/folder_tree.h"
#include "components/aerie_tab_model/smart_folder.h"
#include "components/aerie_tab_model/tab_model.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "ui/aura/window.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/layer_animation_element.h"
#include "ui/compositor/layer_animation_sequence.h"
#include "ui/compositor/layer_animator.h"
#include "ui/compositor/test/layer_animator_test_controller.h"
#include "ui/events/event.h"
#include "ui/gfx/animation/animation_test_api.h"
#include "ui/gfx/geometry/point_conversions.h"
#include "ui/views/view.h"
#include "ui/views/view_observer.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"

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

// Counts the bounds changes of the views it watches. A layout that moves or
// resizes one of them counts once, so a layout per animation frame would show.
class BoundsChangeCounter : public views::ViewObserver {
 public:
  void Watch(views::View* view) {
    observations_.AddObservation(view);
  }
  void OnViewBoundsChanged(views::View*) override { ++count_; }
  void OnViewIsDeleting(views::View* view) override {
    observations_.RemoveObservation(view);
  }
  int count() const { return count_; }

 private:
  int count_ = 0;
  base::ScopedMultiSourceObservation<views::View, views::ViewObserver>
      observations_{this};
};

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

  // The opaque card a floating column shows (AerieSidebarPeekSurface): visible,
  // on a layer of its own, under the rows (it comes before the sidebar view in
  // the column that holds both) and covering the sidebar's rect. Without it the
  // rows slide over the page with no background (issue #237).
  void ExpectTheColumnShowsItsCard() {
    const views::View* card = sidebar()->peek_surface_for_testing();
    ASSERT_TRUE(card);
    EXPECT_TRUE(card->GetVisible());
    EXPECT_TRUE(card->layer());
    ASSERT_EQ(card->parent(), sidebar()->parent());
    EXPECT_LT(*card->parent()->GetIndexOf(card),
              *card->parent()->GetIndexOf(sidebar()));
    EXPECT_TRUE(card->bounds().Contains(sidebar()->bounds()));
  }
  void ExpectTheColumnShowsNoCard() {
    const views::View* card = sidebar()->peek_surface_for_testing();
    EXPECT_TRUE(!card || !card->GetVisible());
  }

  // True only mid-slide of a hide: the column animates, floats, and the layout
  // has given the page its room (the layout runs on the next frame, so the
  // animation alone is true a moment too early). Before the command the column
  // does not animate; after the slide it does not float.
  bool HideIsSliding(int card_width_before) {
    return region()->GetProperty(aerie_sidebar::kFloatingWidth) > 0 &&
           region()->layer()->GetAnimator()->is_animating() &&
           card()->width() > card_width_before;
  }

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

// The tests of the reduced-motion version, run twice: once with the operating
// system's flag and once with the in-app setting aerie.motion.reduce = on
// (issue #177 asks for both).
enum class ReduceVia { kOsFlag, kPref };

class AerieSidebarReducedMotionUiTest
    : public AerieSidebarMotionUiTest,
      public testing::WithParamInterface<ReduceVia> {
 protected:
  void ReduceMotion() {
    if (GetParam() == ReduceVia::kOsFlag) {
      ForceReducedMotion(true);
      return;
    }
    // The OS flag stays off, so only the setting reduces the motion.
    ForceReducedMotion(false);
    prefs()->SetString(aerie::motion::kReduceMotionPref,
                       aerie::motion::kReduceMotionOn);
  }
};

INSTANTIATE_TEST_SUITE_P(
    AerieReduceVia,
    AerieSidebarReducedMotionUiTest,
    testing::Values(ReduceVia::kOsFlag, ReduceVia::kPref),
    [](const testing::TestParamInfo<ReduceVia>& info) {
      return info.param == ReduceVia::kOsFlag ? "OsFlag" : "Pref";
    });

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

IN_PROC_BROWSER_TEST_P(AerieSidebarReducedMotionUiTest, ReducedMotionOpensAndClosesWithoutATransformAndFadesInUnder140ms) {
  ReduceMotion();
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

IN_PROC_BROWSER_TEST_F(AerieSidebarMotionUiTest, HideAndShowAnimateOnlyTheColumnAndTheCardSnaps) {
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

  // The card has its new bounds in one step and never gets a layer of its own:
  // a card transform cannot move the page (see the next test).
  ASSERT_TRUE(base::test::RunUntil([&] { return card()->width() > card_before.width(); }));
  const gfx::Rect card_after = card()->bounds();
  EXPECT_GE(card_after.width() - card_before.width(), column_width - 2);
  EXPECT_FALSE(card()->layer());

  // Nothing lays out per frame: the card's bounds stay what they are for the
  // whole animation.
  ASSERT_TRUE(base::test::RunUntil([&] { return region()->aerie_sidebar_hidden(); }));
  EXPECT_EQ(card()->bounds(), card_after);
  EXPECT_FALSE(card()->layer());

  // Show: the card is narrower at once.
  chrome::ExecuteCommand(browser(), IDC_CTRL_S_SHORTCUT);
  EXPECT_TRUE(Animates(region()->layer(), ui::LayerAnimationElement::TRANSFORM));
  EXPECT_TRUE(OnlyTransformAndOpacityAnimate(region()->layer()));
  ASSERT_TRUE(base::test::RunUntil([&] { return card()->width() < card_after.width(); }));
  const gfx::Rect card_shown = card()->bounds();
  EXPECT_FALSE(card()->layer());
  const base::TimeTicks until = base::TimeTicks::Now() + base::Milliseconds(400);
  base::test::RunUntil([&] { return base::TimeTicks::Now() >= until; });
  EXPECT_EQ(card()->bounds(), card_shown);
}

// A hide that slides draws: the column keeps real bounds, at its own width, for
// the whole slide (a zero-width region left the compositor one frame, issue
// #237), and floats over the room the page has already taken.
IN_PROC_BROWSER_TEST_F(AerieSidebarMotionUiTest, TheHidingColumnKeepsItsWidthAndFloatsOverThePageWhileItSlides) {
  ForceReducedMotion(false);
  ASSERT_TRUE(base::test::RunUntil([&] { return region()->width() > 0; }));
  const int column_width = region()->width();
  const gfx::Rect card_before = card()->bounds();
  EXPECT_EQ(region()->GetProperty(aerie_sidebar::kFloatingWidth), 0);

  chrome::ExecuteCommand(browser(), IDC_CTRL_S_SHORTCUT);
  ASSERT_TRUE(base::test::RunUntil([&] { return HideIsSliding(card_before.width()); }));
  // Mid-slide: the page has its room, the column is still a real, visible
  // region of its own width.
  EXPECT_GT(card()->width(), card_before.width());
  EXPECT_FALSE(region()->aerie_sidebar_hidden());
  EXPECT_TRUE(region()->GetVisible());
  EXPECT_EQ(region()->width(), column_width);
  EXPECT_EQ(region()->GetProperty(aerie_sidebar::kFloatingWidth), column_width);
  ExpectTheColumnShowsItsCard();

  // When the slide ends the column leaves the layout and floats no more, and
  // its card goes with it.
  ASSERT_TRUE(base::test::RunUntil([&] { return region()->aerie_sidebar_hidden(); }));
  EXPECT_EQ(region()->GetProperty(aerie_sidebar::kFloatingWidth), 0);
  ExpectTheColumnShowsNoCard();

  // A show starts from the hidden state and floats nothing (the layout gives
  // the column its room at once), but the opaque card slides in under the rows
  // for the whole of it (issue #248) and goes when the slide has ended.
  chrome::ExecuteCommand(browser(), IDC_CTRL_S_SHORTCUT);
  ASSERT_TRUE(Animates(region()->layer(), ui::LayerAnimationElement::TRANSFORM));
  EXPECT_EQ(region()->GetProperty(aerie_sidebar::kFloatingWidth), 0);
  ExpectTheColumnShowsItsCard();
  const base::TimeTicks until = base::TimeTicks::Now() + base::Milliseconds(60);
  EXPECT_TRUE(base::test::RunUntil([&] { return base::TimeTicks::Now() >= until; }));
  EXPECT_EQ(region()->GetProperty(aerie_sidebar::kFloatingWidth), 0);
  ExpectTheColumnShowsItsCard();
  ASSERT_TRUE(base::test::RunUntil([&] {
    return !region()->layer()->GetAnimator()->is_animating();
  }));
  ExpectTheColumnShowsNoCard();
}

// Reduced motion: the floating column fades and does not slide, within 90 to
// 140 ms, and floats until the fade has ended.
IN_PROC_BROWSER_TEST_F(AerieSidebarMotionUiTest, UnderReducedMotionTheHidingColumnFloatsAndCrossfadesWithoutASlide) {
  ForceReducedMotion(true);
  ASSERT_TRUE(base::test::RunUntil([&] { return region()->width() > 0; }));
  const int column_width = region()->width();
  const gfx::Rect card_before = card()->bounds();
  ui::Layer* column = region()->layer();

  chrome::ExecuteCommand(browser(), IDC_CTRL_S_SHORTCUT);
  ASSERT_TRUE(base::test::RunUntil([&] { return HideIsSliding(card_before.width()); }));
  EXPECT_GT(card()->width(), card_before.width());
  EXPECT_EQ(region()->width(), column_width);
  EXPECT_EQ(region()->GetProperty(aerie_sidebar::kFloatingWidth), column_width);
  EXPECT_TRUE(Animates(column, ui::LayerAnimationElement::OPACITY));
  EXPECT_FALSE(Animates(column, ui::LayerAnimationElement::TRANSFORM));
  EXPECT_TRUE(column->GetTargetTransform().IsIdentity());
  EXPECT_GE(OpacityDuration(column), base::Milliseconds(90));
  EXPECT_LE(OpacityDuration(column), base::Milliseconds(140));
  ExpectTheColumnShowsItsCard();

  ASSERT_TRUE(base::test::RunUntil([&] { return region()->aerie_sidebar_hidden(); }));
  EXPECT_EQ(region()->GetProperty(aerie_sidebar::kFloatingWidth), 0);
  ExpectTheColumnShowsNoCard();
}

// A sidebar on the right floats at the right edge of the window and the page
// card takes the room from its left edge.
IN_PROC_BROWSER_TEST_F(AerieSidebarMotionUiTest, TheHidingColumnOnTheRightFloatsAtTheWindowEdgeOverThePage) {
  ForceReducedMotion(false);
  prefs()->SetString(aerie_sidebar::kSidePref, "right");
  ASSERT_TRUE(base::test::RunUntil([&] {
    return region()->width() > 0 && region()->x() > card()->x();
  }));
  const int column_width = region()->width();
  const gfx::Rect card_before = card()->bounds();
  const int edge = region()->bounds().right();
  EXPECT_EQ(edge, browser_view()->width());

  chrome::ExecuteCommand(browser(), IDC_CTRL_S_SHORTCUT);
  ASSERT_TRUE(base::test::RunUntil([&] { return HideIsSliding(card_before.width()); }));
  EXPECT_EQ(region()->GetProperty(aerie_sidebar::kFloatingWidth), column_width);
  // The column stays at the window edge at its own width, over the page.
  EXPECT_EQ(region()->width(), column_width);
  EXPECT_EQ(region()->bounds().right(), edge);
  // The card keeps its left edge and takes the column's room, to the edge.
  EXPECT_EQ(card()->x(), card_before.x());
  EXPECT_GE(card()->width() - card_before.width(), column_width - 2);
  EXPECT_LE(card()->bounds().right(), edge);
  EXPECT_GT(card()->bounds().right(), region()->x());

  ASSERT_TRUE(base::test::RunUntil([&] { return region()->aerie_sidebar_hidden(); }));
  EXPECT_EQ(region()->GetProperty(aerie_sidebar::kFloatingWidth), 0);
  EXPECT_EQ(card()->x(), card_before.x());
  EXPECT_GE(card()->width() - card_before.width(), column_width - 2);
}

// The layout runs at the two ends of a hide, not once per animation frame: the
// bounds of the column and the page do not change between the first frame of the
// slide and the end.
IN_PROC_BROWSER_TEST_F(AerieSidebarMotionUiTest, AHideLaysOutAtItsBeginAndEndAndNotPerFrame) {
  ForceReducedMotion(false);
  ASSERT_TRUE(base::test::RunUntil([&] { return region()->width() > 0; }));
  const gfx::Rect card_before = card()->bounds();
  BoundsChangeCounter counter;
  counter.Watch(region());
  counter.Watch(card());
  counter.Watch(browser_view()->multi_contents_view()->GetActiveContentsView());

  chrome::ExecuteCommand(browser(), IDC_CTRL_S_SHORTCUT);
  ASSERT_TRUE(base::test::RunUntil([&] { return HideIsSliding(card_before.width()); }));
  const int at_begin = counter.count();
  EXPECT_GE(at_begin, 1);
  EXPECT_LE(at_begin, 6);

  // About a dozen frames of the slide pass.
  const base::TimeTicks until = base::TimeTicks::Now() + base::Milliseconds(80);
  base::test::RunUntil([&] { return base::TimeTicks::Now() >= until; });
  ASSERT_TRUE(HideIsSliding(card_before.width()));
  EXPECT_EQ(counter.count(), at_begin);

  ASSERT_TRUE(base::test::RunUntil([&] { return region()->aerie_sidebar_hidden(); }));
  EXPECT_LE(counter.count() - at_begin, 2);
}

// Where the page really is: the origin of the web contents' native layer in the
// window's root layer, which the compositor draws, against where the view that
// holds the page says it is. The page's native view is a layer of the window,
// not of the card, and the layout places it in window coordinates with the
// card's layer transform folded in. A card that followed the sidebar by a
// transform left the page a column width off after a hide and after a show.
gfx::Point PageLayerOrigin(content::WebContents* contents, views::Widget* widget) {
  gfx::PointF origin;
  ui::Layer::ConvertPointToLayer(contents->GetNativeView()->layer(),
                                 widget->GetLayer(), /*use_target_transform=*/false, &origin);
  return gfx::ToRoundedPoint(origin);
}

IN_PROC_BROWSER_TEST_F(AerieSidebarMotionUiTest, ThePageIsUnderTheCardAfterHideAndShowWithMotion) {
  ForceReducedMotion(false);
  ASSERT_TRUE(base::test::RunUntil([&] { return region()->width() > 0; }));
  views::Widget* widget = browser_view()->GetWidget();
  content::WebContents* contents = browser()->tab_strip_model()->GetActiveWebContents();
  auto expect_the_page_under_the_card = [&](const char* when) {
    // Let the card's follow and its release timer finish.
    const base::TimeTicks until = base::TimeTicks::Now() + base::Milliseconds(700);
    base::test::RunUntil([&] { return base::TimeTicks::Now() >= until; });
    gfx::Point expected =
        browser_view()->multi_contents_view()->GetActiveContentsView()->GetBoundsInScreen().origin();
    expected -= widget->GetWindowBoundsInScreen().OffsetFromOrigin();
    EXPECT_EQ(PageLayerOrigin(contents, widget), expected) << when;
    // And the layer of the page's own view inside it, which the compositor
    // draws the web content in.
    gfx::PointF inner;
    ui::Layer::ConvertPointToLayer(
        contents->GetRenderWidgetHostView()->GetNativeView()->layer(),
        widget->GetLayer(), /*use_target_transform=*/false, &inner);
    EXPECT_EQ(gfx::ToRoundedPoint(inner), expected) << when << " (render widget view)";
  };
  expect_the_page_under_the_card("before");
  chrome::ExecuteCommand(browser(), IDC_CTRL_S_SHORTCUT);
  ASSERT_TRUE(base::test::RunUntil([&] { return region()->aerie_sidebar_hidden(); }));
  expect_the_page_under_the_card("hidden");
  chrome::ExecuteCommand(browser(), IDC_CTRL_S_SHORTCUT);
  expect_the_page_under_the_card("shown again");
}

IN_PROC_BROWSER_TEST_P(AerieSidebarReducedMotionUiTest, ReducedMotionFadesTheColumnAndTheCardSnaps) {
  ReduceMotion();
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

// The tab drag tests (item 8, and issue #182) are in aerie_sidebar_drag_uitest.cc.

namespace {
// Fails when anything in `view` animates a transform; returns the longest
// opacity animation in ms.
int WalkFolderLayers(views::View* view, bool allow_transform, int* longest) {
  if (!view) {
    return *longest;
  }
  if (view->layer()) {
    if (!allow_transform) {
      EXPECT_FALSE(Animates(view->layer(), ui::LayerAnimationElement::TRANSFORM));
    }
    if (Animates(view->layer(), ui::LayerAnimationElement::OPACITY)) {
      *longest = std::max(
          *longest,
          static_cast<int>(OpacityDuration(view->layer()).InMilliseconds()));
    }
  }
  for (views::View* child : view->children()) {
    WalkFolderLayers(child, allow_transform, longest);
  }
  return *longest;
}
}  // namespace

bool FindDescendantAnimating(views::View* view) {
  if (view->layer() && view->layer()->GetAnimator()->is_animating()) {
    return true;
  }
  for (views::View* child : view->children()) {
    if (FindDescendantAnimating(child)) {
      return true;
    }
  }
  return false;
}

// Reduced motion (issue #181): a folder opens and shuts by opacity only, within
// 90 ms, the height does not animate and the chevron does not turn.
IN_PROC_BROWSER_TEST_P(AerieSidebarReducedMotionUiTest,
                       FolderRevealAndCollapseFadeOnlyAndSnapTheHeight) {
  ReduceMotion();
  OpenTab();
  auto* service = aerie_tab_model::TabModelServiceFactory::GetForProfile(
      browser()->GetProfile());
  const std::string tab_id = sidebar()->tree_rows(
      aerie_tab_model::Section::kToday).back().id;
  const std::string folder = sidebar()->CreateFolderIn(
      aerie_tab_model::Section::kToday, std::string());
  ASSERT_FALSE(folder.empty());
  ASSERT_TRUE(aerie_tab_model::MoveTabToFolder(*service->model(), tab_id, folder));
  ASSERT_TRUE(base::test::RunUntil([&] {
    AerieSidebarRowView* row = sidebar()->RowForId(tab_id);
    return row && row->level() == 1;
  }));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return sidebar()->closing_row_count() == 0u &&
           !FindDescendantAnimating(sidebar());
  }));
  for (bool expand : {false, true}) {
    sidebar()->SetFolderExpanded(folder, expand);
    if (sidebar()->folder_group() && sidebar()->folder_group()->running()) {
      EXPECT_EQ(sidebar()->folder_group()->height_animation(), nullptr);
      // Only the rows of the folder: the other rows may still play the open
      // motion of the tab that was just made.
      int longest = 0;
      WalkFolderLayers(sidebar()->RowForId(tab_id), /*allow_transform=*/false,
                       &longest);
      WalkFolderLayers(sidebar()->RowForId(folder), /*allow_transform=*/false,
                       &longest);
      EXPECT_LE(longest, 90);
    }
    // The chevron is at rest at once: no layer, no turn in progress.
    AerieSidebarFolderRowView* folder_row =
      sidebar()->FolderRowForId(folder);
    ASSERT_TRUE(folder_row);
    if (views::View* chevron = folder_row->chevron_view(); chevron->layer()) {
      EXPECT_FALSE(Animates(chevron->layer(),
                            ui::LayerAnimationElement::TRANSFORM));
    }
    ASSERT_TRUE(base::test::RunUntil([&] {
      return !sidebar()->folder_group() || !sidebar()->folder_group()->running();
    }));
  }
}

// Smart folders (issue #140): the rows of a smart folder enter and leave with a
// fade and nothing else moves. The rows are `fast` (140 ms); with reduced motion
// (the OS flag, or the in-app setting) they are 90 ms with no transform.
class AerieSidebarSmartMotionUiTest : public AerieSidebarReducedMotionUiTest {
 protected:
  void SetUpOnMainThread() override {
    embedded_test_server()->ServeFilesFromSourceDirectory("chrome/test/data");
    ASSERT_TRUE(embedded_test_server()->Start());
    AerieSidebarReducedMotionUiTest::SetUpOnMainThread();
  }

  // A tab on a host the smart folder's rule matches, and the folder.
  std::string OpenMatchingTabAndMakeFolder(std::string* folder) {
    EXPECT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
        browser(), embedded_test_server()->GetURL("docs.localhost", "/title1.html"),
        WindowOpenDisposition::kNewForegroundTab,
        ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
    auto* service = aerie_tab_model::TabModelServiceFactory::GetForProfile(
        browser()->GetProfile());
    const std::string tab =
        service->adapter()->RecordIdForTab(browser()->tab_strip_model()->GetActiveTab());
    // Reduced motion and the folder's own reveal are set before the folder is
    // made, so that the rows' motion is the one that is read.
    ReduceMotion();
    *folder = aerie_tab_model::CreateFolder(*service->model(),
                                            aerie_tab_model::Section::kToday,
                                            std::string(), "Docs");
    EXPECT_TRUE(aerie_tab_model::SetFolderRuleKind(
        *service->model(), *folder, aerie_tab_model::FolderKind::kSmart));
    EXPECT_FALSE(aerie_tab_model::AddRule(*service->model(), *folder,
                                          aerie_tab_model::RuleType::kDomain,
                                          "docs.localhost")
                     .empty());
    return tab;
  }

  AerieSidebarRowView* WaitForSmartRow(const std::string& id) {
    EXPECT_TRUE(base::test::RunUntil(
        [&] { return sidebar()->RowForId(id) != nullptr; }));
    return sidebar()->RowForId(id);
  }
};

IN_PROC_BROWSER_TEST_P(AerieSidebarSmartMotionUiTest,
                       SmartRowsFadeInAndOutWithNoTransformAndTheReducedDurationIs90ms) {
  std::string folder;
  const std::string tab = OpenMatchingTabAndMakeFolder(&folder);
  AerieSidebarRowView* row =
      WaitForSmartRow(aerie_tab_model::MakeSmartRowId(folder, tab));
  ASSERT_TRUE(row);
  ASSERT_TRUE(row->smart_member());
  ASSERT_TRUE(row->content()->layer());
  ui::Layer* layer = row->content()->layer();
  // Only opacity animates on the way in: no transform, and the row does not
  // grow in height by itself.
  EXPECT_TRUE(Animates(layer, ui::LayerAnimationElement::OPACITY));
  EXPECT_FALSE(Animates(layer, ui::LayerAnimationElement::TRANSFORM));
  EXPECT_TRUE(OnlyTransformAndOpacityAnimate(layer));
  EXPECT_TRUE(layer->GetTargetTransform().IsIdentity());
  EXPECT_EQ(layer->GetTargetOpacity(), 1.f);
  EXPECT_EQ(OpacityDuration(layer), base::Milliseconds(90));
  ASSERT_TRUE(base::test::RunUntil([&] { return !row->height_animating(); }));

  // The real tab closes, and the smart row fades out the same way.
  auto* strip = browser()->tab_strip_model();
  strip->CloseWebContentsAt(strip->active_index(), TabCloseTypes::CLOSE_NONE);
  AerieSidebarRowView* leaving = nullptr;
  ASSERT_TRUE(base::test::RunUntil([&] {
    for (size_t i = 0; i < sidebar()->closing_row_count(); ++i) {
      if (sidebar()->closing_row_at(i)->smart_member()) {
        leaving = sidebar()->closing_row_at(i);
      }
    }
    return leaving != nullptr;
  }));
  ASSERT_TRUE(leaving->content()->layer());
  ui::Layer* leaving_layer = leaving->content()->layer();
  EXPECT_FALSE(Animates(leaving_layer, ui::LayerAnimationElement::TRANSFORM));
  EXPECT_TRUE(OnlyTransformAndOpacityAnimate(leaving_layer));
  EXPECT_TRUE(leaving_layer->GetTargetTransform().IsIdentity());
  EXPECT_EQ(leaving_layer->GetTargetOpacity(), 0.f);
  EXPECT_EQ(OpacityDuration(leaving_layer), base::Milliseconds(90));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return sidebar()->closing_row_count() == 0u; }));
}

INSTANTIATE_TEST_SUITE_P(
    AerieSmartReduceVia,
    AerieSidebarSmartMotionUiTest,
    testing::Values(ReduceVia::kOsFlag, ReduceVia::kPref),
    [](const testing::TestParamInfo<ReduceVia>& info) {
      return info.param == ReduceVia::kOsFlag ? "OsFlag" : "Pref";
    });

// Without reduced motion a smart row fades on `fast` (140 ms) and nothing else
// moves either.
IN_PROC_BROWSER_TEST_F(AerieSidebarMotionUiTest, SmartRowsFadeOnFastWithoutReducedMotionAndNothingElseMoves) {
  ASSERT_TRUE(embedded_test_server()->Start());
  ForceReducedMotion(false);
  EXPECT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
      browser(), embedded_test_server()->GetURL("docs.localhost", "/title1.html"),
      WindowOpenDisposition::kNewForegroundTab,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
  auto* service = aerie_tab_model::TabModelServiceFactory::GetForProfile(
      browser()->GetProfile());
  const std::string tab =
      service->adapter()->RecordIdForTab(browser()->tab_strip_model()->GetActiveTab());
  ASSERT_TRUE(base::test::RunUntil([&] {
    return !sidebar()->RowForId(tab) || !sidebar()->RowForId(tab)->height_animating();
  }));
  const std::string folder = aerie_tab_model::CreateFolder(
      *service->model(), aerie_tab_model::Section::kToday, std::string(), "Docs");
  ASSERT_TRUE(aerie_tab_model::SetFolderRuleKind(
      *service->model(), folder, aerie_tab_model::FolderKind::kSmart));
  ASSERT_FALSE(aerie_tab_model::AddRule(*service->model(), folder,
                                        aerie_tab_model::RuleType::kDomain,
                                        "docs.localhost")
                   .empty());
  const std::string id = aerie_tab_model::MakeSmartRowId(folder, tab);
  ASSERT_TRUE(base::test::RunUntil([&] { return sidebar()->RowForId(id) != nullptr; }));
  AerieSidebarRowView* row = sidebar()->RowForId(id);
  ASSERT_TRUE(row->content()->layer());
  ui::Layer* layer = row->content()->layer();
  EXPECT_TRUE(Animates(layer, ui::LayerAnimationElement::OPACITY));
  EXPECT_FALSE(Animates(layer, ui::LayerAnimationElement::TRANSFORM));
  EXPECT_TRUE(OnlyTransformAndOpacityAnimate(layer));
  EXPECT_EQ(OpacityDuration(layer), base::Milliseconds(140));
}

// The reveal and the collapse of a smart folder (issue #140): its rows take
// part in the folder motion like the rows of any folder. They are found by the
// sync as inside the toggled folder (IsInsideFolder knows the smart row ids), so
// a reveal is the 200 ms fade with the 15 ms stagger and the height, and a
// collapse holds the rows until the fade has played.
namespace {

// The smart rows that are in the list now, the ones held for a collapse
// included (they are no longer the sidebar's own rows, but still on screen).
std::vector<AerieSidebarRowView*> SmartRowsInList(AerieSidebarView* sidebar) {
  std::vector<AerieSidebarRowView*> found;
  for (views::View* child : sidebar->today_tree()->children()) {
    auto* row = views::AsViewClass<AerieSidebarRowView>(child);
    if (row && row->smart_member()) {
      found.push_back(row);
    }
  }
  return found;
}

struct SmartSetup {
  std::string folder;
  std::vector<std::string> rows;
};

}  // namespace

class AerieSidebarSmartFolderMotionUiTest : public AerieSidebarReducedMotionUiTest {
 protected:
  void SetUpOnMainThread() override {
    embedded_test_server()->ServeFilesFromSourceDirectory("chrome/test/data");
    ASSERT_TRUE(embedded_test_server()->Start());
    AerieSidebarReducedMotionUiTest::SetUpOnMainThread();
  }

  // A smart folder with two member rows, at rest.
  SmartSetup MakeSettledSmartFolder() {
    SmartSetup setup;
    auto* service = aerie_tab_model::TabModelServiceFactory::GetForProfile(
        browser()->GetProfile());
    std::vector<std::string> tabs;
    for (const char* path : {"/title1.html", "/title2.html"}) {
      EXPECT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
          browser(), embedded_test_server()->GetURL("docs.localhost", path),
          WindowOpenDisposition::kNewForegroundTab,
          ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
      tabs.push_back(service->adapter()->RecordIdForTab(
          browser()->tab_strip_model()->GetActiveTab()));
    }
    setup.folder = aerie_tab_model::CreateFolder(
        *service->model(), aerie_tab_model::Section::kToday, std::string(),
        "Docs");
    EXPECT_TRUE(aerie_tab_model::SetFolderRuleKind(
        *service->model(), setup.folder, aerie_tab_model::FolderKind::kSmart));
    EXPECT_FALSE(aerie_tab_model::AddRule(*service->model(), setup.folder,
                                          aerie_tab_model::RuleType::kDomain,
                                          "docs.localhost")
                     .empty());
    for (const std::string& tab : tabs) {
      setup.rows.push_back(aerie_tab_model::MakeSmartRowId(setup.folder, tab));
    }
    EXPECT_TRUE(base::test::RunUntil([&] {
      return sidebar()->RowForId(setup.rows[0]) &&
             sidebar()->RowForId(setup.rows[1]);
    }));
    WaitForRest();
    return setup;
  }

  void WaitForRest() {
    EXPECT_TRUE(base::test::RunUntil([&] {
      return sidebar()->closing_row_count() == 0u &&
             !(sidebar()->folder_group() && sidebar()->folder_group()->running()) &&
             !FindDescendantAnimating(sidebar());
    }));
  }

  void ExpectFadeOnly(AerieSidebarRowView* row, base::TimeDelta duration) {
    ASSERT_TRUE(row->content()->layer());
    ui::Layer* layer = row->content()->layer();
    EXPECT_TRUE(Animates(layer, ui::LayerAnimationElement::OPACITY));
    EXPECT_FALSE(Animates(layer, ui::LayerAnimationElement::TRANSFORM));
    EXPECT_TRUE(OnlyTransformAndOpacityAnimate(layer));
    EXPECT_EQ(OpacityDuration(layer), duration);
  }
};

IN_PROC_BROWSER_TEST_F(AerieSidebarMotionUiTest,
                       ASmartFolderRevealsWithTheStaggerAndCollapsesHeldForTheFade) {
  ASSERT_TRUE(embedded_test_server()->Start());
  ForceReducedMotion(false);
  auto* service = aerie_tab_model::TabModelServiceFactory::GetForProfile(
      browser()->GetProfile());
  std::vector<std::string> tabs;
  for (const char* path : {"/title1.html", "/title2.html"}) {
    ASSERT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
        browser(), embedded_test_server()->GetURL("docs.localhost", path),
        WindowOpenDisposition::kNewForegroundTab,
        ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
    tabs.push_back(service->adapter()->RecordIdForTab(
        browser()->tab_strip_model()->GetActiveTab()));
  }
  const std::string folder = aerie_tab_model::CreateFolder(
      *service->model(), aerie_tab_model::Section::kToday, std::string(), "Docs");
  ASSERT_TRUE(aerie_tab_model::SetFolderRuleKind(
      *service->model(), folder, aerie_tab_model::FolderKind::kSmart));
  ASSERT_FALSE(aerie_tab_model::AddRule(*service->model(), folder,
                                        aerie_tab_model::RuleType::kDomain,
                                        "docs.localhost")
                   .empty());
  const std::string a = aerie_tab_model::MakeSmartRowId(folder, tabs[0]);
  const std::string b = aerie_tab_model::MakeSmartRowId(folder, tabs[1]);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return sidebar()->RowForId(a) && sidebar()->RowForId(b); }));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return sidebar()->closing_row_count() == 0u &&
           !(sidebar()->folder_group() && sidebar()->folder_group()->running()) &&
           !FindDescendantAnimating(sidebar());
  }));

  // Collapse: the rows are held for the fade (90 ms), then the height (to
  // 180 ms in all), and only then go.
  sidebar()->SetFolderExpanded(folder, false);
  const AerieSidebarFolderGroup* group = sidebar()->folder_group();
  ASSERT_TRUE(group && group->running());
  EXPECT_FALSE(group->reveal());
  EXPECT_EQ(group->row_count(), 2u);
  // Held: the rows are still in the list while the fade plays.
  const std::vector<AerieSidebarRowView*> held = SmartRowsInList(sidebar());
  ASSERT_EQ(held.size(), 2u);
  for (AerieSidebarRowView* row : held) {
    ui::Layer* layer = row->content()->layer();
    ASSERT_TRUE(layer);
    EXPECT_TRUE(Animates(layer, ui::LayerAnimationElement::OPACITY));
    EXPECT_TRUE(OnlyTransformAndOpacityAnimate(layer));
    EXPECT_EQ(OpacityDuration(layer), base::Milliseconds(90));
  }
  EXPECT_NE(group->height_animation(), nullptr);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return SmartRowsInList(sidebar()).empty(); }));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return !(sidebar()->folder_group() && sidebar()->folder_group()->running()) &&
           !FindDescendantAnimating(sidebar());
  }));

  // Reveal: 200 ms fade, the second row 15 ms after the first, and the height.
  sidebar()->SetFolderExpanded(folder, true);
  group = sidebar()->folder_group();
  ASSERT_TRUE(group && group->running());
  EXPECT_TRUE(group->reveal());
  EXPECT_EQ(group->row_count(), 2u);
  EXPECT_NE(group->height_animation(), nullptr);
  AerieSidebarRowView* first = sidebar()->RowForId(a);
  AerieSidebarRowView* second = sidebar()->RowForId(b);
  ASSERT_TRUE(first && second);
  ASSERT_TRUE(first->content()->layer() && second->content()->layer());
  EXPECT_TRUE(Animates(first->content()->layer(), ui::LayerAnimationElement::OPACITY));
  EXPECT_EQ(OpacityDuration(first->content()->layer()), base::Milliseconds(200));
  // The second waits out the stagger: still at 0, with its timer set.
  EXPECT_EQ(second->content()->layer()->opacity(), 0.f);
  EXPECT_TRUE(second->folder_fading());
  EXPECT_FALSE(Animates(second->content()->layer(), ui::LayerAnimationElement::OPACITY));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return Animates(second->content()->layer(), ui::LayerAnimationElement::OPACITY);
  }));
  EXPECT_EQ(OpacityDuration(second->content()->layer()), base::Milliseconds(200));
  EXPECT_FALSE(Animates(second->content()->layer(), ui::LayerAnimationElement::TRANSFORM));
  EXPECT_TRUE(OnlyTransformAndOpacityAnimate(second->content()->layer()));
}

// Reduced motion (the OS flag, and the in-app setting): 90 ms opacity, no
// stagger, no transform and no height animation, for the reveal and the collapse.
IN_PROC_BROWSER_TEST_P(AerieSidebarSmartFolderMotionUiTest,
                       ReducedMotionRevealsAndCollapsesASmartFolderWithoutStaggerOrTransform) {
  ReduceMotion();
  const SmartSetup setup = MakeSettledSmartFolder();
  const std::string& a = setup.rows[0];
  const std::string& b = setup.rows[1];

  sidebar()->SetFolderExpanded(setup.folder, false);
  const AerieSidebarFolderGroup* group = sidebar()->folder_group();
  ASSERT_TRUE(group && group->running());
  EXPECT_FALSE(group->reveal());
  EXPECT_EQ(group->row_count(), 2u);
  EXPECT_EQ(group->height_animation(), nullptr);
  const std::vector<AerieSidebarRowView*> held = SmartRowsInList(sidebar());
  ASSERT_EQ(held.size(), 2u);
  for (AerieSidebarRowView* row : held) {
    ExpectFadeOnly(row, base::Milliseconds(90));
  }
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return SmartRowsInList(sidebar()).empty(); }));
  WaitForRest();

  sidebar()->SetFolderExpanded(setup.folder, true);
  group = sidebar()->folder_group();
  ASSERT_TRUE(group && group->running());
  EXPECT_TRUE(group->reveal());
  EXPECT_EQ(group->row_count(), 2u);
  EXPECT_EQ(group->height_animation(), nullptr);
  ASSERT_TRUE(sidebar()->RowForId(a) && sidebar()->RowForId(b));
  // Both fade at once: no stagger.
  ExpectFadeOnly(sidebar()->RowForId(a), base::Milliseconds(90));
  ExpectFadeOnly(sidebar()->RowForId(b), base::Milliseconds(90));
}

INSTANTIATE_TEST_SUITE_P(
    AerieSmartFolderReduceVia,
    AerieSidebarSmartFolderMotionUiTest,
    testing::Values(ReduceVia::kOsFlag, ReduceVia::kPref),
    [](const testing::TestParamInfo<ReduceVia>& info) {
      return info.param == ReduceVia::kOsFlag ? "OsFlag" : "Pref";
    });

// The card and the rows move as one (issue #248). A show or a hide has one
// layer, the region's, that carries the slide or the fade; the opaque card and
// the rows are under it and do not animate on their own. The test reads where
// each is on screen (bounds in screen coordinates, with the layer transforms
// applied) and its effective opacity at several moments of the slide: each is
// at its resting place plus the region's own translation, the offset between
// the card and a row never changes, and their opacities are equal. A show used
// to have no card at all: the column's tint was the window frame, already at
// its final place, and only the rows slid over it.
namespace {

float EffectiveOpacity(const views::View* view) {
  for (const views::View* v = view; v; v = v->parent()) {
    if (v->layer()) {
      return v->layer()->GetCombinedOpacity();
    }
  }
  return 1.f;
}

struct SlideSample {
  bool card_visible;
  const ui::Layer* card_layer;
  const ui::Layer* sidebar_layer;
  int card_x;
  int row_x;
  int sidebar_x;
  float card_opacity;
  float row_opacity;
  float region_opacity;
  int region_tx;
};

struct RestingPlace {
  int row_x;
  int sidebar_x;
};

}  // namespace

class AerieSidebarStepUiTest : public AerieSidebarMotionUiTest {
 protected:
  void Wait(base::TimeDelta delay) {
    base::RunLoop loop;
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, loop.QuitClosure(), delay);
    loop.Run();
  }

  SlideSample Sample() {
    const views::View* card = sidebar()->peek_surface_for_testing();
    SlideSample s{};
    s.card_visible = card && card->GetVisible();
    s.card_layer = card ? card->layer() : nullptr;
    s.sidebar_layer = sidebar()->layer();
    s.card_x = card ? card->GetBoundsInScreen().x() : 0;
    s.card_opacity = card ? EffectiveOpacity(card) : -1.f;
    s.row_x = sidebar()->row_at(0)->GetBoundsInScreen().x();
    s.row_opacity = EffectiveOpacity(sidebar()->row_at(0));
    s.sidebar_x = sidebar()->GetBoundsInScreen().x();
    s.region_opacity = region()->layer()->opacity();
    s.region_tx = static_cast<int>(
        region()->layer()->transform().To2dTranslation().x());
    return s;
  }

  RestingPlace Resting() {
    return {sidebar()->row_at(0)->GetBoundsInScreen().x(),
            sidebar()->GetBoundsInScreen().x()};
  }

  // Samples until the region's layer has stopped animating (or 1.5 s). The
  // first sample is taken at once, before any frame ran. Samples are 4 ms
  // apart: a reduced fade lasts 90 ms or more, so a slow box still gets
  // several of them.
  std::vector<SlideSample> SampleUntilRest() {
    std::vector<SlideSample> all;
    all.push_back(Sample());
    const base::TimeTicks give_up =
        base::TimeTicks::Now() + base::Milliseconds(1500);
    bool started = region()->layer()->GetAnimator()->is_animating();
    while (base::TimeTicks::Now() < give_up) {
      Wait(base::Milliseconds(4));
      if (region()->layer()->GetAnimator()->is_animating()) {
        started = true;
        all.push_back(Sample());
      } else if (started) {
        break;
      }
    }
    return all;
  }

  // Presses Ctrl+S and samples until the slide has ended.
  std::vector<SlideSample> ToggleAndSample() {
    chrome::ExecuteCommand(browser(), IDC_CTRL_S_SHORTCUT);
    return SampleUntilRest();
  }

  // The samples taken while the slide was in flight: the column part way
  // between its two places (a transform) or part way through its fade.
  static std::vector<SlideSample> InFlight(
      const std::vector<SlideSample>& all,
      bool reduced) {
    int max_tx = 0;
    for (const SlideSample& s : all) {
      max_tx = std::max(max_tx, std::abs(s.region_tx));
    }
    std::vector<SlideSample> in_flight;
    for (const SlideSample& s : all) {
      const bool part_way =
          reduced ? (s.region_opacity > 0.02f && s.region_opacity < 0.98f)
                  : (std::abs(s.region_tx) >= 1 &&
                     std::abs(s.region_tx) < max_tx);
      if (part_way) {
        in_flight.push_back(s);
      }
    }
    return in_flight;
  }

  void ExpectOneMotion(const std::vector<SlideSample>& all,
                       const RestingPlace& rest,
                       bool reduced) {
    // The card is there from the first frame the region's transform or opacity
    // is off its resting value; nothing is filtered out for this check.
    for (const SlideSample& s : all) {
      if (s.region_tx != 0 || s.region_opacity != 1.f) {
        EXPECT_TRUE(s.card_visible) << "no card on the first frame off rest";
        break;
      }
    }
    const std::vector<SlideSample> in_flight = InFlight(all, reduced);
    ASSERT_GE(in_flight.size(), 2u) << "the slide had fewer than two frames";
    int moved = 0;
    for (const SlideSample& s : in_flight) {
      SCOPED_TRACE(testing::Message()
                   << "region_tx=" << s.region_tx
                   << " region_opacity=" << s.region_opacity);
      EXPECT_TRUE(s.card_visible) << "no card under the rows";
      if (!s.card_visible) {
        continue;
      }
      // Each is at its resting place plus the region's translation: a group
      // that lags the region, or a part of it on a clock of its own, is off.
      EXPECT_NEAR(s.card_x,
                  rest.sidebar_x - AerieSidebarPeekSurface::ShadowRoom() +
                      s.region_tx,
                  1);
      EXPECT_NEAR(s.row_x, rest.row_x + s.region_tx, 1);
      EXPECT_NEAR(s.card_opacity, s.row_opacity, 0.001f);
      EXPECT_NEAR(s.card_opacity, s.region_opacity, 0.001f);
    }
    for (size_t i = 1; i < in_flight.size(); ++i) {
      if (in_flight[i].card_x != in_flight[i - 1].card_x) {
        ++moved;
      }
    }
    if (reduced) {
      EXPECT_EQ(moved, 0) << "a reduced slide must not move";
      EXPECT_NE(in_flight.front().card_opacity, in_flight.back().card_opacity);
    } else {
      EXPECT_GE(moved, 1) << "the card did not move";
    }
  }

  void RunHideAndShow(bool reduced) {
    ForceReducedMotion(reduced);
    ASSERT_TRUE(base::test::RunUntil([&] { return region()->width() > 0; }));
    const RestingPlace rest = Resting();
    {
      SCOPED_TRACE("hide");
      ExpectOneMotion(ToggleAndSample(), rest, reduced);
    }
    ASSERT_TRUE(base::test::RunUntil(
        [&] { return region()->aerie_sidebar_hidden(); }));
    {
      SCOPED_TRACE("show");
      ExpectOneMotion(ToggleAndSample(), rest, reduced);
    }
  }

  // Every sample from `first` on has the card up, on the same layers as the
  // first one: the card and the sidebar's layer are not made again.
  static void ExpectTheCardStaysUp(const std::vector<SlideSample>& samples) {
    ASSERT_FALSE(samples.empty());
    const ui::Layer* card_layer = samples.front().card_layer;
    const ui::Layer* sidebar_layer = samples.front().sidebar_layer;
    EXPECT_TRUE(card_layer);
    EXPECT_TRUE(sidebar_layer);
    for (const SlideSample& s : samples) {
      EXPECT_TRUE(s.card_visible);
      EXPECT_EQ(s.card_layer, card_layer);
      EXPECT_EQ(s.sidebar_layer, sidebar_layer);
    }
  }
};

IN_PROC_BROWSER_TEST_F(AerieSidebarStepUiTest, TheCardAndTheRowsMoveAsOneOnHideAndShow) {
  RunHideAndShow(/*reduced=*/false);
}

IN_PROC_BROWSER_TEST_F(AerieSidebarStepUiTest, TheCardAndTheRowsFadeAsOneOnHideAndShowUnderReducedMotion) {
  RunHideAndShow(/*reduced=*/true);
}

// The region's layer is made again (a new layer, a new animator): the card is
// still there for the first show after it.
IN_PROC_BROWSER_TEST_F(AerieSidebarStepUiTest, TheCardShowsOnTheFirstShowAfterTheRegionLayerIsRecreated) {
  ForceReducedMotion(false);
  ASSERT_TRUE(base::test::RunUntil([&] { return region()->width() > 0; }));
  const RestingPlace rest = Resting();
  chrome::ExecuteCommand(browser(), IDC_CTRL_S_SHORTCUT);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return region()->aerie_sidebar_hidden(); }));
  std::unique_ptr<ui::Layer> old_layer = region()->RecreateLayer();
  ASSERT_TRUE(region()->layer());
  ASSERT_NE(region()->layer(), old_layer.get());
  // The first slide of the new layer is a show.
  ExpectOneMotion(ToggleAndSample(), rest, /*reduced=*/false);
}

// A show that starts while a hide is in flight, and a hide that starts while a
// show is: the old animation goes before the new one starts, and the card and
// the sidebar's layer stay as they were across the hand-over.
IN_PROC_BROWSER_TEST_F(AerieSidebarStepUiTest, TheCardStaysUpWhenAShowInterruptsAHideAndAHideInterruptsAShow) {
  ForceReducedMotion(false);
  ASSERT_TRUE(base::test::RunUntil([&] { return region()->width() > 0; }));
  const RestingPlace rest = Resting();

  // Hide, then a show 60 ms in.
  chrome::ExecuteCommand(browser(), IDC_CTRL_S_SHORTCUT);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return region()->layer()->GetAnimator()->is_animating();
  }));
  Wait(base::Milliseconds(60));
  ASSERT_FALSE(region()->aerie_sidebar_hidden());
  std::vector<SlideSample> samples;
  samples.push_back(Sample());
  chrome::ExecuteCommand(browser(), IDC_CTRL_S_SHORTCUT);
  for (const SlideSample& s : SampleUntilRest()) {
    samples.push_back(s);
  }
  ExpectTheCardStaysUp(samples);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return !region()->layer()->GetAnimator()->is_animating();
  }));
  EXPECT_FALSE(region()->aerie_sidebar_hidden());
  EXPECT_EQ(Resting().row_x, rest.row_x);

  // Hide to the end, then a show, then a hide 60 ms into it.
  chrome::ExecuteCommand(browser(), IDC_CTRL_S_SHORTCUT);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return region()->aerie_sidebar_hidden(); }));
  chrome::ExecuteCommand(browser(), IDC_CTRL_S_SHORTCUT);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return region()->layer()->GetAnimator()->is_animating();
  }));
  Wait(base::Milliseconds(60));
  samples.clear();
  samples.push_back(Sample());
  chrome::ExecuteCommand(browser(), IDC_CTRL_S_SHORTCUT);
  for (const SlideSample& s : SampleUntilRest()) {
    samples.push_back(s);
  }
  ExpectTheCardStaysUp(samples);
}
