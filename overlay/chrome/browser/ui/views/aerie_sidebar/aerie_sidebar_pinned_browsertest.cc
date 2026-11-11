// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

// Browser tests of Pinned tabs and Favorites (issue #180): they survive a
// restart without loading, the back-to-pinned-page button, the 13th Favorite,
// the three pinned-close modes, the context menu, and what a screen reader
// hears. PRE_ tests are the first run of a pair; the next run is the restart.

#include <optional>
#include <string>

#include "base/test/run_until.h"
#include "chrome/browser/aerie_tab_model/pinned_tabs_controller.h"
#include "chrome/browser/aerie_tab_model/sleep_controller.h"
#include "chrome/browser/aerie_tab_model/sleep_controller_factory.h"
#include "chrome/browser/aerie_tab_model/tab_model_service.h"
#include "chrome/browser/aerie_tab_model/tab_model_service_factory.h"
#include "chrome/browser/aerie_tab_model/tab_strip_adapter.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_context_menu.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_regions.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_row_view.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_tile_view.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_view.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"
#include "chrome/grit/generated_resources.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/aerie_tab_model/pinned_tabs.h"
#include "components/aerie_tab_model/tab_model.h"
#include "components/aerie_tab_model/tab_model_prefs.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/mojom/menu_source_type.mojom.h"
#include "ui/gfx/animation/animation_test_api.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/test/button_test_api.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/menu/menu_item_view.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"

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

ui::AXNodeData DataOf(views::View* view) {
  ui::AXNodeData data;
  view->GetViewAccessibility().GetAccessibleNodeData(&data);
  return data;
}

std::string NameOf(views::View* view) {
  return DataOf(view).GetStringAttribute(ax::mojom::StringAttribute::kName);
}

}  // namespace

class AerieSidebarPinnedBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    host_resolver()->AddRule("*", "127.0.0.1");
    ASSERT_TRUE(embedded_test_server()->Start());
    reduced_motion_.emplace(
        gfx::AnimationTestApi::SetPrefersReducedMotionForTesting(true));
  }
  void TearDownOnMainThread() override {
    reduced_motion_.reset();
    InProcessBrowserTest::TearDownOnMainThread();
  }

  AerieSidebarView* sidebar() {
    return FindDescendant<AerieSidebarView>(
        BrowserView::GetBrowserViewForBrowser(browser())
            ->vertical_tab_strip_region_view_for_testing());
  }
  TabStripModel* strip() { return browser()->tab_strip_model(); }
  aerie_tab_model::TabModelService* service() {
    return aerie_tab_model::TabModelServiceFactory::GetForProfile(
        browser()->GetProfile());
  }
  aerie_tab_model::TabModel* model() { return service()->model(); }
  aerie_tab_model::PinnedTabsController* pinned() {
    return service()->pinned_tabs();
  }
  GURL Url(const std::string& path) {
    return embedded_test_server()->GetURL(path);
  }

  // The rows follow the Tab Model one task later: wait until the Today list
  // shows every Today record (a tab that was just pinned leaves it).
  void WaitForRowsSynced() {
    EXPECT_TRUE(base::test::RunUntil([&] {
      return sidebar()->row_count() ==
             aerie_tab_model::SectionTabIds(*model(),
                                            aerie_tab_model::Section::kToday)
                 .size();
    }));
  }

  // Opens a page in a new foreground tab and waits for its row.
  tabs::TabInterface* OpenTab(const std::string& path) {
    EXPECT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
        browser(), Url(path), WindowOpenDisposition::NEW_FOREGROUND_TAB,
        ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
    WaitForRowsSynced();
    return strip()->GetTabAtIndex(strip()->active_index());
  }
  // A blank page that has a committed URL, which loads at once: for the tests that need many tabs.
  tabs::TabInterface* OpenBlankTab() {
    EXPECT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
        browser(), GURL("about:blank"), WindowOpenDisposition::NEW_FOREGROUND_TAB,
        ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
    WaitForRowsSynced();
    return strip()->GetTabAtIndex(strip()->active_index());
  }
  std::string IdOf(tabs::TabInterface* tab) {
    return service()->adapter()->RecordIdForTab(tab);
  }
  void SetCloseMode(const char* mode) {
    browser()->GetProfile()->GetPrefs()->SetString(
        aerie_tab_model::kPinnedClose, mode);
  }
  void WaitForPinned(size_t count) {
    ASSERT_TRUE(base::test::RunUntil(
        [&] { return sidebar()->pinned_row_count() == count; }));
  }
  void WaitForFavorites(size_t count) {
    ASSERT_TRUE(base::test::RunUntil(
        [&] { return sidebar()->favorite_tile_count() == count; }));
  }

  void Click(views::Button* button) {
    views::test::ButtonTestApi(button).NotifyClick(ui::MouseEvent(
        ui::EventType::kMousePressed, gfx::Point(), gfx::Point(),
        base::TimeTicks(), ui::EF_LEFT_MOUSE_BUTTON, ui::EF_LEFT_MOUSE_BUTTON));
  }

  std::optional<gfx::AnimationTestApi::PrefersReducedMotionResetter>
      reduced_motion_;
};

// Acceptance: Pinned tabs and Favorites come back after a restart, without
// loading a page. The PRE_ run pins one tab and favorites another; 20 pinned
// tabs make the second test.
IN_PROC_BROWSER_TEST_F(AerieSidebarPinnedBrowserTest,
                       PRE_PinnedAndFavoritesComeBackUnloaded) {
  tabs::TabInterface* a = OpenTab("/title1.html");
  tabs::TabInterface* b = OpenTab("/title2.html");
  ASSERT_TRUE(pinned()->Pin(a));
  ASSERT_EQ(pinned()->AddToFavorites(b),
            aerie_tab_model::FavoriteResult::kAdded);
  WaitForPinned(1);
  WaitForFavorites(1);
}

IN_PROC_BROWSER_TEST_F(AerieSidebarPinnedBrowserTest,
                       PinnedAndFavoritesComeBackUnloaded) {
  ASSERT_TRUE(base::test::RunUntil([&] {
    return sidebar()->pinned_row_count() == 1 &&
           sidebar()->favorite_tile_count() == 1;
  }));
  // Both are unloaded: no navigation happened, and they show as asleep.
  EXPECT_TRUE(sidebar()->pinned_row_at(0)->sleeping());
  EXPECT_TRUE(sidebar()->tile_at(0)->sleeping());
  for (int i = 0; i < strip()->count(); ++i) {
    tabs::TabInterface* tab = strip()->GetTabAtIndex(i);
    const std::string id = IdOf(tab);
    const auto* record = model()->GetTab(id);
    if (record && record->section != aerie_tab_model::Section::kToday) {
      EXPECT_TRUE(tab->GetContents()->WasDiscarded() ||
                  tab->GetContents()->GetController().NeedsReload());
    }
  }
  // Selecting the pinned row loads its page.
  tabs::TabInterface* tab =
      service()->adapter()->TabForRecord(sidebar()->pinned_tab_id_at(0));
  ASSERT_TRUE(tab);
  strip()->ActivateTabAt(strip()->GetIndexOfTab(tab));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return !sidebar()->pinned_row_at(0)->sleeping(); }));
}

IN_PROC_BROWSER_TEST_F(AerieSidebarPinnedBrowserTest,
                       PRE_TwentyPinnedTabsComeBackAndTodayStaysReachable) {
  for (int i = 0; i < 20; ++i) {
    ASSERT_TRUE(pinned()->Pin(OpenBlankTab()));
  }
  WaitForPinned(20);
}

IN_PROC_BROWSER_TEST_F(AerieSidebarPinnedBrowserTest,
                       TwentyPinnedTabsComeBackAndTodayStaysReachable) {
  EXPECT_TRUE(base::test::RunUntil(
      [&] { return sidebar()->pinned_row_count() == 20; }))
      << "pinned rows: " << sidebar()->pinned_row_count() << ", tabs: "
      << strip()->count() << ", records: "
      << aerie_tab_model::SectionTabIds(*model(),
                                        aerie_tab_model::Section::kPinned)
             .size()
      << ", loaded: " << service()->loaded();
  ASSERT_EQ(sidebar()->pinned_row_count(), 20u);
  // The Pinned list scrolls on its own, so it never takes the room of Today.
  // (The headless window has no real height, so this reads the preferred
  // height of the Pinned list: it is capped well under 20 rows.)
  views::View* pinned_scroll = sidebar()->pinned_tree()->parent()->parent();
  const int row_height = sidebar()->pinned_row_at(0)->GetPreferredSize().height();
  EXPECT_LT(pinned_scroll->GetPreferredSize().height(), 10 * row_height);
  EXPECT_GE(pinned_scroll->GetPreferredSize().height(), 3 * row_height);
  EXPECT_TRUE(sidebar()->today_tree()->GetVisible());
}

// Acceptance: the Chromium pinned bit is never set; Aerie keeps its own.
IN_PROC_BROWSER_TEST_F(AerieSidebarPinnedBrowserTest, NoTabCarriesTheChromiumPinnedBit) {
  tabs::TabInterface* tab = OpenTab("/title1.html");
  ASSERT_TRUE(pinned()->Pin(tab));
  WaitForPinned(1);
  for (int i = 0; i < strip()->count(); ++i) {
    EXPECT_FALSE(strip()->IsTabPinned(i));
  }
}

// Acceptance: a pinned tab that left its page shows "Back to pinned page", and
// the button takes it back.
IN_PROC_BROWSER_TEST_F(AerieSidebarPinnedBrowserTest, BackToPinnedPageShowsAfterLeavingAndGoesBack) {
  tabs::TabInterface* tab = OpenTab("/title1.html");
  ASSERT_TRUE(pinned()->Pin(tab));
  WaitForPinned(1);
  AerieSidebarRowView* row = sidebar()->pinned_row_at(0);
  EXPECT_FALSE(row->back_to_pinned_button()->GetVisible());
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("/title2.html")));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return row->back_to_pinned_button()->GetVisible(); }));
  EXPECT_EQ(NameOf(row->back_to_pinned_button()),
            l10n_util::GetStringUTF8(IDS_AERIE_SIDEBAR_BACK_TO_PINNED));
  Click(row->back_to_pinned_button());
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return !row->back_to_pinned_button()->GetVisible(); }));
  EXPECT_EQ(tab->GetContents()->GetLastCommittedURL().path(), "/title1.html");
}

// Acceptance: the 13th Favorite is refused, nothing changes, and the toast says
// why.
IN_PROC_BROWSER_TEST_F(AerieSidebarPinnedBrowserTest, TheThirteenthFavoriteIsRefusedWithAToast) {
  for (int i = 0; i < 12; ++i) {
    ASSERT_EQ(pinned()->AddToFavorites(OpenBlankTab()),
              aerie_tab_model::FavoriteResult::kAdded);
  }
  WaitForFavorites(12);
  tabs::TabInterface* extra = OpenTab("/title2.html");
  // Through the real menu: the Today row of the extra tab.
  ASSERT_GE(sidebar()->row_count(), 1u);
  size_t index = 0;
  for (size_t i = 0; i < sidebar()->row_count(); ++i) {
    if (sidebar()->tab_id_at(i) == IdOf(extra)) {
      index = i;
    }
  }
  sidebar()->row_at(index)->ShowContextMenu(
      gfx::Point(5, 5), ui::mojom::MenuSourceType::kMouse);
  ASSERT_TRUE(sidebar()->context_menu());
  sidebar()->context_menu()->ExecuteCommand(
      AerieSidebarContextMenu::kCommandAddToFavorites);
  sidebar()->context_menu()->Close();
  EXPECT_EQ(sidebar()->favorite_tile_count(), 12u);
  ASSERT_TRUE(sidebar()->toast_visible());
  EXPECT_EQ(base::UTF16ToUTF8(sidebar()->toast()->GetText()),
            l10n_util::GetStringUTF8(IDS_AERIE_SIDEBAR_FAVORITES_FULL));
  EXPECT_EQ(DataOf(sidebar()->toast()).role, ax::mojom::Role::kStatus);
}

// Acceptance: the three close modes, tooltips included.
IN_PROC_BROWSER_TEST_F(AerieSidebarPinnedBrowserTest, CloseModeResetAndSleepGoesBackThenSleeps) {
  SetCloseMode(aerie_tab_model::kPinnedCloseResetSleepSwitch);
  tabs::TabInterface* tab = OpenTab("/title1.html");
  ASSERT_TRUE(pinned()->Pin(tab));
  WaitForPinned(1);
  AerieSidebarRowView* row = sidebar()->pinned_row_at(0);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("/title2.html")));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return row->back_to_pinned_button()->GetVisible(); }));
  EXPECT_EQ(row->close_button()->GetRenderedTooltipText(gfx::Point()),
            l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_CLOSE_RESET_SLEEP));
  const int tabs_before = strip()->count();
  Click(row->close_button());
  ASSERT_TRUE(base::test::RunUntil([&] { return row->sleeping(); }));
  // The tab stays: nothing was closed, the page is the pinned one.
  EXPECT_EQ(strip()->count(), tabs_before);
  EXPECT_EQ(strip()->count(), tabs_before);
}

IN_PROC_BROWSER_TEST_F(AerieSidebarPinnedBrowserTest, CloseModeSleepKeepsThePageAndSleeps) {
  SetCloseMode(aerie_tab_model::kPinnedCloseSleep);
  tabs::TabInterface* tab = OpenTab("/title1.html");
  ASSERT_TRUE(pinned()->Pin(tab));
  WaitForPinned(1);
  AerieSidebarRowView* row = sidebar()->pinned_row_at(0);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("/title2.html")));
  EXPECT_EQ(row->close_button()->GetRenderedTooltipText(gfx::Point()),
            l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_CLOSE_SLEEP));
  Click(row->close_button());
  ASSERT_TRUE(base::test::RunUntil([&] { return row->sleeping(); }));
  EXPECT_EQ(sidebar()->pinned_row_count(), 1u);
}

IN_PROC_BROWSER_TEST_F(AerieSidebarPinnedBrowserTest, CloseModeCloseUnpinsAndCloses) {
  SetCloseMode(aerie_tab_model::kPinnedCloseClose);
  tabs::TabInterface* tab = OpenTab("/title1.html");
  ASSERT_TRUE(pinned()->Pin(tab));
  WaitForPinned(1);
  AerieSidebarRowView* row = sidebar()->pinned_row_at(0);
  EXPECT_EQ(row->close_button()->GetRenderedTooltipText(gfx::Point()),
            l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_CLOSE_CLOSE));
  const int tabs_before = strip()->count();
  Click(row->close_button());
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return strip()->count() == tabs_before - 1; }));
  WaitForPinned(0);
}

// Acceptance: Remove from Favorites returns the tab to where it was.
IN_PROC_BROWSER_TEST_F(AerieSidebarPinnedBrowserTest, RemoveFromFavoritesReturnsToThePreviousSection) {
  tabs::TabInterface* tab = OpenTab("/title1.html");
  ASSERT_TRUE(pinned()->Pin(tab));
  ASSERT_EQ(pinned()->AddToFavorites(tab),
            aerie_tab_model::FavoriteResult::kAdded);
  WaitForFavorites(1);
  WaitForPinned(0);
  ASSERT_TRUE(pinned()->RemoveFromFavorites(tab));
  WaitForFavorites(0);
  WaitForPinned(1);
}

// The context menu items by section.
IN_PROC_BROWSER_TEST_F(AerieSidebarPinnedBrowserTest, ContextMenuItemsFollowTheSection) {
  using M = AerieSidebarContextMenu;
  AerieSidebarContextMenu::Options options;
  options.section = aerie_tab_model::Section::kToday;
  M today(options, base::DoNothing());
  EXPECT_NE(today.item(M::kCommandPin), nullptr);
  EXPECT_EQ(today.item(M::kCommandUnpin), nullptr);
  EXPECT_NE(today.item(M::kCommandAddToFavorites), nullptr);
  EXPECT_NE(today.item(M::kCommandSleepTab), nullptr);
  EXPECT_NE(today.item(M::kCommandClose), nullptr);
  options.section = aerie_tab_model::Section::kPinned;
  M pin(options, base::DoNothing());
  EXPECT_NE(pin.item(M::kCommandUnpin), nullptr);
  EXPECT_EQ(pin.item(M::kCommandPin), nullptr);
  options.section = aerie_tab_model::Section::kFavorite;
  M fav(options, base::DoNothing());
  EXPECT_NE(fav.item(M::kCommandRemoveFromFavorites), nullptr);
  EXPECT_EQ(fav.item(M::kCommandAddToFavorites), nullptr);
}

// Roles and names for a screen reader.
IN_PROC_BROWSER_TEST_F(AerieSidebarPinnedBrowserTest, RolesAndNames) {
  tabs::TabInterface* a = OpenTab("/title1.html");
  tabs::TabInterface* b = OpenTab("/title2.html");
  ASSERT_TRUE(pinned()->Pin(a));
  ASSERT_EQ(pinned()->AddToFavorites(b),
            aerie_tab_model::FavoriteResult::kAdded);
  WaitForPinned(1);
  WaitForFavorites(1);
  EXPECT_EQ(DataOf(sidebar()->favorites_grid()).role, ax::mojom::Role::kGrid);
  EXPECT_EQ(NameOf(sidebar()->favorites_grid()),
            l10n_util::GetStringUTF8(IDS_AERIE_SIDEBAR_FAVORITES));
  EXPECT_EQ(DataOf(sidebar()->pinned_tree()).role, ax::mojom::Role::kTree);
  EXPECT_EQ(NameOf(sidebar()->pinned_tree()),
            l10n_util::GetStringUTF8(IDS_AERIE_SIDEBAR_PINNED));
  EXPECT_EQ(DataOf(sidebar()->tile_at(0)).role, ax::mojom::Role::kButton);
  EXPECT_FALSE(NameOf(sidebar()->tile_at(0)).empty());
}

// Acceptance: pinned and favorite tabs are never auto-archived. The archive
// rule (issue #176) only reads Today records; this pins the documented
// behaviour in a test.
IN_PROC_BROWSER_TEST_F(AerieSidebarPinnedBrowserTest, PinnedAndFavoriteTabsAreNeverArchivable) {
  tabs::TabInterface* a = OpenTab("/title1.html");
  ASSERT_TRUE(pinned()->Pin(a));
  const auto* record = model()->GetTab(IdOf(a));
  ASSERT_TRUE(record);
  EXPECT_EQ(record->section, aerie_tab_model::Section::kPinned);
  // The auto-archive rule (issue #176) reads Today tabs only; a pinned record
  // is never marked archived.
  EXPECT_FALSE(record->archived_at.has_value());
}
