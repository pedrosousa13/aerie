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
#include "ui/compositor/layer_animator.h"
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
  void Press(ui::KeyboardCode key) {
    ASSERT_TRUE(ui_test_utils::SendKeyPressSync(browser(), key, false, false,
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
  EXPECT_FALSE(AnimatesTransform(sidebar()->pinned_row_at(0)->layer()));
  EXPECT_FALSE(AnimatesTransform(sidebar()->tile_at(0)->layer()));
  // Nothing animates for longer than the reduced opacity limit.
  EXPECT_LE(aerie::motion::kMaxReducedOpacityMs, 140);
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
  sidebar()->context_menu()->Close();
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
  Press(ui::VKEY_APPS);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return sidebar()->context_menu() && sidebar()->context_menu()->is_running();
  }));
  EXPECT_NE(sidebar()->context_menu()->item(AerieSidebarContextMenu::kCommandUnpin),
            nullptr);
  Press(ui::VKEY_ESCAPE);
  EXPECT_TRUE(base::test::RunUntil(
      [&] { return !sidebar()->context_menu()->is_running(); }));
}
