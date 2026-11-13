// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

// Interactive UI tests of Pinned tabs and Favorites (issue #180): the FLIP
// motion of a tab that changes section and its reduced-motion form (the
// operating system's flag, and the in-app setting), the context menu open
// motion, and the keyboard: arrows in the Favorites grid and the Pinned tree,
// and the Menu key. Run with ./aerie test-cpp --target
// aerie_interactive_ui_tests.

#include <optional>
#include <string>

#include "base/test/run_until.h"
#include "chrome/browser/aerie_tab_model/pinned_tabs_controller.h"
#include "chrome/browser/aerie_tab_model/tab_model_service.h"
#include "chrome/browser/aerie_tab_model/tab_model_service_factory.h"
#include "chrome/browser/aerie_tab_model/tab_strip_adapter.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "content/public/browser/web_contents.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_context_menu.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_row_view.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_tile_view.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_view.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/interactive_test_utils.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/aerie_design_tokens/aerie_motion.h"
#include "components/aerie_tab_model/pinned_tabs.h"
#include "components/aerie_tab_model/tab_model.h"
#include "components/aerie_design_tokens/aerie_motion_prefs.h"
#include "components/aerie_design_tokens/aerie_tokens.h"
#include "components/prefs/pref_service.h"
#include "content/public/test/browser_test.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "ui/base/mojom/menu_source_type.mojom.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/layer_animation_element.h"
#include "ui/compositor/layer_animation_sequence.h"
#include "ui/compositor/layer_animator.h"
#include "ui/compositor/test/layer_animator_test_controller.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/animation/animation_test_api.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"

namespace {

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

bool AnimatesTransform(ui::Layer* layer) {
  return layer && layer->GetAnimator()->IsAnimatingProperty(
                      ui::LayerAnimationElement::TRANSFORM);
}

// The real duration of the opacity animation that runs on `layer`, in ms; -1
// when none runs.
int OpacityDurationMs(ui::Layer* layer) {
  ui::LayerAnimatorTestController controller(layer->GetAnimator());
  ui::LayerAnimationSequence* sequence =
      controller.GetRunningSequence(ui::LayerAnimationElement::OPACITY);
  return sequence ? static_cast<int>(
                        sequence->FirstElement()->duration().InMilliseconds())
                  : -1;
}

int ReducedFastMs() {
  return aerie::tokens::GetDurationMs(aerie::tokens::MotionDuration::kFast,
                                      /*reduced=*/true);
}

}  // namespace

class AerieSidebarPinnedUiTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));
    host_resolver()->AddRule("*", "127.0.0.1");
    ASSERT_TRUE(embedded_test_server()->Start());
    ASSERT_TRUE(base::test::RunUntil(
        [&] { return sidebar() && sidebar()->row_count() == 1u; }));
  }
  void TearDownOnMainThread() override {
    reduced_motion_.reset();
    InProcessBrowserTest::TearDownOnMainThread();
  }

  void ForceReducedMotion(bool reduced) {
    reduced_motion_.reset();
    reduced_motion_.emplace(
        gfx::AnimationTestApi::SetPrefersReducedMotionForTesting(reduced));
  }
  AerieSidebarView* sidebar() {
    return FindDescendant<AerieSidebarView>(
        BrowserView::GetBrowserViewForBrowser(browser())
            ->vertical_tab_strip_region_view_for_testing());
  }
  aerie_tab_model::TabModelService* service() {
    return aerie_tab_model::TabModelServiceFactory::GetForProfile(
        browser()->GetProfile());
  }
  views::View* focused() {
    return BrowserView::GetBrowserViewForBrowser(browser())
        ->GetWidget()
        ->GetFocusManager()
        ->GetFocusedView();
  }
  void Press(ui::KeyboardCode key, bool control = false, bool shift = false) {
    ASSERT_TRUE(ui_test_utils::SendKeyPressSync(browser(), key, control, shift,
                                                false, false));
  }
  tabs::TabInterface* OpenTab() {
    EXPECT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
        browser(), embedded_test_server()->GetURL("/title1.html"),
        WindowOpenDisposition::NEW_BACKGROUND_TAB,
        ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
    // The rows follow the Tab Model one task later; a tab that was just pinned
    // leaves the Today list.
    EXPECT_TRUE(base::test::RunUntil([&] {
      return sidebar()->row_count() ==
             aerie_tab_model::SectionTabIds(*service()->model(),
                                            aerie_tab_model::Section::kToday)
                 .size();
    }));
    return browser()->tab_strip_model()->GetTabAtIndex(
        browser()->tab_strip_model()->count() - 1);
  }

  std::optional<gfx::AnimationTestApi::PrefersReducedMotionResetter>
      reduced_motion_;
};

IN_PROC_BROWSER_TEST_F(AerieSidebarPinnedUiTest, PinningFlipsTheRowOnTheSpring) {
  ForceReducedMotion(false);
  tabs::TabInterface* tab = OpenTab();
  ASSERT_TRUE(service()->pinned_tabs()->Pin(tab));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return sidebar()->pinned_row_count() == 1u; }));
  AerieSidebarRowView* row = sidebar()->pinned_row_at(0);
  // The moved row has a layer that animates a transform (only that and its
  // own opacity), for the spring duration.
  EXPECT_TRUE(row->layer());
  EXPECT_TRUE(AnimatesTransform(row->layer()));
}

IN_PROC_BROWSER_TEST_F(AerieSidebarPinnedUiTest, FavoritingFlipsTheTile) {
  ForceReducedMotion(false);
  tabs::TabInterface* tab = OpenTab();
  ASSERT_EQ(service()->pinned_tabs()->AddToFavorites(tab),
            aerie_tab_model::FavoriteResult::kAdded);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return sidebar()->favorite_tile_count() == 1u; }));
  AerieSidebarTileView* tile = sidebar()->tile_at(0);
  EXPECT_TRUE(tile->layer());
  EXPECT_TRUE(AnimatesTransform(tile->layer()));
}

enum class ReduceVia { kOsFlag, kPref };

class AerieSidebarPinnedReducedMotionUiTest
    : public AerieSidebarPinnedUiTest,
      public testing::WithParamInterface<ReduceVia> {};

INSTANTIATE_TEST_SUITE_P(
    AerieReduceVia,
    AerieSidebarPinnedReducedMotionUiTest,
    testing::Values(ReduceVia::kOsFlag, ReduceVia::kPref),
    [](const testing::TestParamInfo<ReduceVia>& info) {
      return info.param == ReduceVia::kOsFlag ? "OsFlag" : "Pref";
    });

IN_PROC_BROWSER_TEST_P(AerieSidebarPinnedReducedMotionUiTest,
                       ReducedMotionMovesTheRowAndTileAtOnceWithNoTransform) {
  if (GetParam() == ReduceVia::kOsFlag) {
    ForceReducedMotion(true);
  } else {
    ForceReducedMotion(false);
    browser()->GetProfile()->GetPrefs()->SetString(
        aerie::motion::kReduceMotionPref, aerie::motion::kReduceMotionOn);
  }
  tabs::TabInterface* a = OpenTab();
  tabs::TabInterface* b = OpenTab();
  ASSERT_TRUE(service()->pinned_tabs()->Pin(a));
  ASSERT_EQ(service()->pinned_tabs()->AddToFavorites(b),
            aerie_tab_model::FavoriteResult::kAdded);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return sidebar()->pinned_row_count() == 1u &&
           sidebar()->favorite_tile_count() == 1u;
  }));
  for (ui::Layer* layer :
       {sidebar()->pinned_row_at(0)->layer(), sidebar()->tile_at(0)->layer()}) {
    // No transform animation on the row or the tile; if an opacity animation
    // runs at all, it is no longer than the reduced limit.
    if (!layer) {
      continue;
    }
    EXPECT_FALSE(AnimatesTransform(layer));
    const int ms = OpacityDurationMs(layer);
    EXPECT_LE(ms, aerie::motion::kMaxReducedOpacityMs);
  }

  // The context menu fades in on the reduced `fast` duration and does not scale.
  sidebar()->row_at(0)->ShowContextMenu(gfx::Point(5, 5),
                                        ui::mojom::MenuSourceType::kMouse);
  ASSERT_TRUE(sidebar()->context_menu());
  ui::Layer* menu_layer = sidebar()->context_menu()->open_layer_for_testing();
  ASSERT_TRUE(menu_layer);
  EXPECT_FALSE(AnimatesTransform(menu_layer));
  EXPECT_EQ(OpacityDurationMs(menu_layer), ReducedFastMs());
  EXPECT_LE(OpacityDurationMs(menu_layer), aerie::motion::kMaxReducedOpacityMs);
  sidebar()->context_menu()->Close();

  // The toast only fades, in and out, on the same duration.
  sidebar()->ShowToastForTesting(u"Toast");
  ASSERT_TRUE(sidebar()->toast_layer());
  EXPECT_FALSE(AnimatesTransform(sidebar()->toast_layer()));
  EXPECT_TRUE(sidebar()->toast_layer()->transform().IsIdentity());
  EXPECT_EQ(OpacityDurationMs(sidebar()->toast_layer()), ReducedFastMs());
  sidebar()->HideToastForTesting();
  ASSERT_TRUE(sidebar()->toast_layer());
  EXPECT_FALSE(AnimatesTransform(sidebar()->toast_layer()));
  EXPECT_LE(OpacityDurationMs(sidebar()->toast_layer()),
            aerie::motion::kMaxReducedOpacityMs);
}

IN_PROC_BROWSER_TEST_F(AerieSidebarPinnedUiTest, TheContextMenuOpensFromNinetySevenPercentAndFadesIn) {
  ForceReducedMotion(false);
  sidebar()->row_at(0)->ShowContextMenu(gfx::Point(5, 5),
                                        ui::mojom::MenuSourceType::kMouse);
  ASSERT_TRUE(sidebar()->context_menu());
  ASSERT_TRUE(sidebar()->context_menu()->is_running());
  ui::Layer* layer = sidebar()->context_menu()->open_layer_for_testing();
  ASSERT_TRUE(layer);
  EXPECT_TRUE(AnimatesTransform(layer));
  EXPECT_TRUE(layer->GetAnimator()->IsAnimatingProperty(
      ui::LayerAnimationElement::OPACITY));
  EXPECT_TRUE(layer->GetTargetTransform().IsIdentity());
  EXPECT_EQ(OpacityDurationMs(layer),
            aerie::tokens::GetDurationMs(aerie::tokens::MotionDuration::kFast,
                                         false));
  sidebar()->context_menu()->Close();
}

// The toast rises 6 px while it fades in (fast, decelerate), holds for
// `motion-timing-toast` (a toast without actions) and fades out (fast, accelerate) without moving.
IN_PROC_BROWSER_TEST_F(AerieSidebarPinnedUiTest, TheToastRisesSixPixelsAndFadesInThenFadesOutWithoutMoving) {
  ForceReducedMotion(false);
  sidebar()->ShowToastForTesting(u"Toast");
  ui::Layer* layer = sidebar()->toast_layer();
  ASSERT_TRUE(layer);
  EXPECT_TRUE(AnimatesTransform(layer));
  EXPECT_TRUE(layer->GetAnimator()->IsAnimatingProperty(
      ui::LayerAnimationElement::OPACITY));
  EXPECT_FLOAT_EQ(layer->transform().To2dTranslation().y(),
                  static_cast<float>(aerie::tokens::kMotionDistanceToastRise));
  EXPECT_TRUE(layer->GetTargetTransform().IsIdentity());
  EXPECT_FLOAT_EQ(layer->GetTargetOpacity(), 1.f);
  EXPECT_EQ(OpacityDurationMs(layer),
            aerie::tokens::GetDurationMs(aerie::tokens::MotionDuration::kFast,
                                         false));
  EXPECT_EQ(aerie::tokens::kMotionTimingToast, 1600);

  sidebar()->HideToastForTesting();
  layer = sidebar()->toast_layer();
  ASSERT_TRUE(layer);
  EXPECT_FALSE(AnimatesTransform(layer));
  EXPECT_FLOAT_EQ(layer->GetTargetOpacity(), 0.f);
  EXPECT_EQ(OpacityDurationMs(layer),
            aerie::tokens::GetDurationMs(aerie::tokens::MotionDuration::kFast,
                                         false));
  // The label goes when the fade has ended.
  ASSERT_TRUE(base::test::RunUntil([&] { return !sidebar()->toast_visible(); }));
}

IN_PROC_BROWSER_TEST_F(AerieSidebarPinnedUiTest, UnderReducedMotionTheContextMenuOnlyFadesIn) {
  ForceReducedMotion(true);
  sidebar()->row_at(0)->ShowContextMenu(gfx::Point(5, 5),
                                        ui::mojom::MenuSourceType::kMouse);
  ASSERT_TRUE(sidebar()->context_menu());
  ui::Layer* layer = sidebar()->context_menu()->open_layer_for_testing();
  ASSERT_TRUE(layer);
  EXPECT_FALSE(AnimatesTransform(layer));
  EXPECT_TRUE(layer->transform().IsIdentity());
  sidebar()->context_menu()->Close();
}

IN_PROC_BROWSER_TEST_F(AerieSidebarPinnedUiTest, ArrowsMoveInTheFavoritesGridInTwoDimensions) {
  ForceReducedMotion(true);
  for (int i = 0; i < 6; ++i) {
    ASSERT_EQ(service()->pinned_tabs()->AddToFavorites(OpenTab()),
              aerie_tab_model::FavoriteResult::kAdded);
  }
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return sidebar()->favorite_tile_count() == 6u; }));
  sidebar()->favorites_grid()->RequestFocus();
  ASSERT_EQ(focused(), sidebar()->tile_at(0));
  Press(ui::VKEY_RIGHT);
  EXPECT_EQ(focused(), sidebar()->tile_at(1));
  Press(ui::VKEY_DOWN);  // 4 columns: tile 1 -> tile 5.
  EXPECT_EQ(focused(), sidebar()->tile_at(5));
  Press(ui::VKEY_UP);
  EXPECT_EQ(focused(), sidebar()->tile_at(1));
  Press(ui::VKEY_END);
  EXPECT_EQ(focused(), sidebar()->tile_at(3));
  Press(ui::VKEY_HOME);
  EXPECT_EQ(focused(), sidebar()->tile_at(0));
  // One Tab stop: the others are not focusable by Tab.
  int stops = 0;
  for (size_t i = 0; i < sidebar()->favorite_tile_count(); ++i) {
    stops += sidebar()->tile_at(i)->is_tab_stop() ? 1 : 0;
  }
  EXPECT_EQ(stops, 1);
}

IN_PROC_BROWSER_TEST_F(AerieSidebarPinnedUiTest, ArrowsMoveInThePinnedTreeAndTheMenuKeyOpensTheMenu) {
  ForceReducedMotion(true);
  for (int i = 0; i < 3; ++i) {
    ASSERT_TRUE(service()->pinned_tabs()->Pin(OpenTab()));
  }
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return sidebar()->pinned_row_count() == 3u; }));
  sidebar()->pinned_tree()->RequestFocus();
  ASSERT_EQ(focused(), sidebar()->pinned_row_at(0));
  Press(ui::VKEY_DOWN);
  EXPECT_EQ(focused(), sidebar()->pinned_row_at(1));
  Press(ui::VKEY_END);
  EXPECT_EQ(focused(), sidebar()->pinned_row_at(2));
  // The tree is one Tab stop.
  int stops = 0;
  for (size_t i = 0; i < sidebar()->pinned_row_count(); ++i) {
    stops += sidebar()->pinned_row_at(i)->is_tab_stop() ? 1 : 0;
  }
  EXPECT_EQ(stops, 1);
  Press(ui::VKEY_APPS);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return sidebar()->context_menu() && sidebar()->context_menu()->is_running();
  }));
  EXPECT_NE(sidebar()->context_menu()->item(AerieSidebarContextMenu::Command::kUnpin),
            nullptr);
  Press(ui::VKEY_ESCAPE);
  EXPECT_TRUE(base::test::RunUntil(
      [&] { return !sidebar()->context_menu()->is_running(); }));
}

// Enter and Delete on a focused tile go through the real view.
IN_PROC_BROWSER_TEST_F(AerieSidebarPinnedUiTest, EnterSelectsAndDeleteClosesAsTheSettingSaysOnAFavoriteTile) {
  ForceReducedMotion(true);
  tabs::TabInterface* a = OpenTab();
  tabs::TabInterface* b = OpenTab();
  ASSERT_EQ(service()->pinned_tabs()->AddToFavorites(a),
            aerie_tab_model::FavoriteResult::kAdded);
  ASSERT_EQ(service()->pinned_tabs()->AddToFavorites(b),
            aerie_tab_model::FavoriteResult::kAdded);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return sidebar()->favorite_tile_count() == 2u; }));
  sidebar()->favorites_grid()->RequestFocus();
  Press(ui::VKEY_RIGHT);
  ASSERT_EQ(focused(), sidebar()->tile_at(1));
  const std::string id = sidebar()->favorite_tab_id_at(1);
  tabs::TabInterface* tab = service()->adapter()->TabForRecord(id);
  Press(ui::VKEY_RETURN);
  EXPECT_EQ(browser()->tab_strip_model()->GetActiveTab(), tab);
  // Delete on the other tile: the default setting puts it to sleep, the tile
  // stays (the second press would close it).
  // Selecting a tab moves focus to its page: back to the grid, and one tile left.
  sidebar()->favorites_grid()->RequestFocus();
  ASSERT_EQ(focused(), sidebar()->tile_at(1));
  Press(ui::VKEY_LEFT);
  ASSERT_EQ(focused(), sidebar()->tile_at(0));
  Press(ui::VKEY_DELETE);
  EXPECT_TRUE(base::test::RunUntil([&] { return sidebar()->tile_at(0)->sleeping(); }));
  EXPECT_EQ(sidebar()->favorite_tile_count(), 2u);
}

// Backspace on a focused pinned row that has left its page is the keyboard way
// back (the button has no focus stop).
IN_PROC_BROWSER_TEST_F(AerieSidebarPinnedUiTest, BackspaceOnAPinnedRowThatLeftItsPageGoesBackToIt) {
  ForceReducedMotion(true);
  tabs::TabInterface* tab = OpenTab();
  ASSERT_TRUE(service()->pinned_tabs()->Pin(tab));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return sidebar()->pinned_row_count() == 1u; }));
  browser()->tab_strip_model()->ActivateTabAt(
      browser()->tab_strip_model()->GetIndexOfTab(tab));
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title2.html")));
  AerieSidebarRowView* row = sidebar()->pinned_row_at(0);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return row->back_to_pinned_button()->GetVisible(); }));
  sidebar()->pinned_tree()->RequestFocus();
  ASSERT_EQ(focused(), row);
  // With a modifier held the key does nothing.
  Press(ui::VKEY_BACK, /*control=*/true);
  Press(ui::VKEY_BACK, /*control=*/false, /*shift=*/true);
  EXPECT_TRUE(row->back_to_pinned_button()->GetVisible());
  EXPECT_EQ(tab->GetContents()->GetLastCommittedURL().path(), "/title2.html");
  Press(ui::VKEY_BACK);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return !row->back_to_pinned_button()->GetVisible(); }));
  EXPECT_EQ(tab->GetContents()->GetLastCommittedURL().path(), "/title1.html");
  // At its page the key does nothing.
  Press(ui::VKEY_BACK);
  EXPECT_EQ(tab->GetContents()->GetLastCommittedURL().path(), "/title1.html");
}
