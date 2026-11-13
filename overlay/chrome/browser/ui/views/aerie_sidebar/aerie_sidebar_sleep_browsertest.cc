// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

// Browser tests of how the sidebar shows sleeping tabs (issue #179): the row's
// 0.55 favicon, the static desaturate, the moon badge and ", asleep" in the
// accessible name; the count and the estimate in the Today header; a tab that
// Chromium discards itself; and the "Sleep tab" item of the context menu.

#include <optional>
#include <string>

#include "base/run_loop.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "base/strings/string_number_conversions.h"
#include "base/test/run_until.h"
#include "chrome/browser/aerie_tab_model/sleep_controller.h"
#include "chrome/browser/aerie_tab_model/sleep_controller_factory.h"
#include "chrome/browser/aerie_tab_model/tab_model_service.h"
#include "chrome/browser/aerie_tab_model/tab_model_service_factory.h"
#include "chrome/browser/aerie_tab_model/tab_strip_adapter.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/resource_coordinator/lifecycle_unit_state.mojom.h"
#include "chrome/browser/resource_coordinator/tab_lifecycle_unit_external.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_context_menu.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_row_view.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_view.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"
#include "chrome/grit/generated_resources.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/aerie_design_tokens/aerie_tokens.h"
#include "components/aerie_tab_model/tab_model.h"
#include "content/public/test/browser_test.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/mojom/menu_source_type.mojom.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/animation/animation_test_api.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/image_view.h"
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

std::string NameOf(views::View* view) {
  ui::AXNodeData data;
  view->GetViewAccessibility().GetAccessibleNodeData(&data);
  return data.GetStringAttribute(ax::mojom::StringAttribute::kName);
}

}  // namespace

class AerieSidebarSleepBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    host_resolver()->AddRule("*", "127.0.0.1");
    ASSERT_TRUE(embedded_test_server()->Start());
    // The instant look: motion is tested in the interactive tests.
    reduced_motion_.emplace(
        gfx::AnimationTestApi::SetPrefersReducedMotionForTesting(true));
    controller_ = aerie_tab_model::SleepControllerFactory::GetForProfile(
        browser()->GetProfile());
    ASSERT_TRUE(controller_);
  }

  void TearDownOnMainThread() override {
    controller_ = nullptr;
    reduced_motion_.reset();
    InProcessBrowserTest::TearDownOnMainThread();
  }

  AerieSidebarView* sidebar() {
    return FindDescendant<AerieSidebarView>(
        BrowserView::GetBrowserViewForBrowser(browser())
            ->vertical_tab_strip_region_view_for_testing());
  }
  TabStripModel* strip() { return browser()->tab_strip_model(); }

  // A loaded tab behind the selected one; returns its index.
  int OpenBackgroundTab() {
    EXPECT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
        browser(), embedded_test_server()->GetURL("/title1.html"),
        WindowOpenDisposition::NEW_BACKGROUND_TAB,
        ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
    const int index = strip()->count() - 1;
    EXPECT_TRUE(base::test::RunUntil(
        [&] { return sidebar()->row_count() == static_cast<size_t>(index) + 1; }));
    return index;
  }

  AerieSidebarRowView* RowAt(int index) {
    return sidebar()->row_at(static_cast<size_t>(index));
  }

  void SleepTab(int index) {
    ASSERT_TRUE(controller_->Sleep(strip()->GetTabAtIndex(index)));
    ASSERT_TRUE(base::test::RunUntil([&] { return RowAt(index)->sleeping(); }));
  }

 private:
  std::optional<gfx::AnimationTestApi::PrefersReducedMotionResetter>
      reduced_motion_;

 protected:
  raw_ptr<aerie_tab_model::SleepController> controller_ = nullptr;
};

IN_PROC_BROWSER_TEST_F(AerieSidebarSleepBrowserTest,
                       AwakeRowHasNoBadgeAndNoLayerOnItsFavicon) {
  const int index = OpenBackgroundTab();
  AerieSidebarRowView* row = RowAt(index);
  EXPECT_FALSE(row->sleeping());
  EXPECT_FALSE(row->sleep_badge()->GetVisible());
  EXPECT_EQ(row->favicon_view()->layer(), nullptr);
  const std::string name = NameOf(row);
  EXPECT_FALSE(name.empty());
  EXPECT_FALSE(name.ends_with(", asleep"));
}

IN_PROC_BROWSER_TEST_F(AerieSidebarSleepBrowserTest,
                       ASleepingRowShowsTheTokenOpacityTheDesaturateTheBadgeAndTheName) {
  const int index = OpenBackgroundTab();
  const std::string awake_name = NameOf(RowAt(index));
  SleepTab(index);

  AerieSidebarRowView* row = RowAt(index);
  ASSERT_TRUE(row->favicon_view()->layer());
  EXPECT_FLOAT_EQ(row->favicon_view()->layer()->GetTargetOpacity(),
                  aerie::tokens::kMotionOpacitySleeping);
  EXPECT_FLOAT_EQ(row->favicon_view()->layer()->GetTargetOpacity(), 0.55f);
  // A static filter, not an animation.
  EXPECT_FLOAT_EQ(row->favicon_view()->layer()->GetTargetGrayscale(), 1.f);
  EXPECT_TRUE(row->sleep_badge()->GetVisible());
  EXPECT_EQ(NameOf(row), awake_name + ", asleep");
  // The title text is not dimmed: only the favicon is.
  // The tab that is selected is not.
  EXPECT_FALSE(RowAt(0)->sleeping());
}

IN_PROC_BROWSER_TEST_F(AerieSidebarSleepBrowserTest,
                       SelectingASleepingRowWakesItAndTheLookGoesBack) {
  const int index = OpenBackgroundTab();
  SleepTab(index);
  strip()->ActivateTabAt(index);
  ASSERT_TRUE(base::test::RunUntil([&] { return !RowAt(index)->sleeping(); }));
  EXPECT_FALSE(RowAt(index)->sleep_badge()->GetVisible());
  EXPECT_EQ(RowAt(index)->favicon_view()->layer(), nullptr);
  EXPECT_FALSE(NameOf(RowAt(index)).ends_with(", asleep"));
}

IN_PROC_BROWSER_TEST_F(AerieSidebarSleepBrowserTest,
                       ATabThatChromiumDiscardsForMemoryIsShownAsAsleep) {
  const int index = OpenBackgroundTab();
  auto* lifecycle =
      resource_coordinator::TabLifecycleUnitExternal::FromWebContents(
          strip()->GetWebContentsAt(index));
  ASSERT_TRUE(lifecycle);
  ASSERT_TRUE(lifecycle->DiscardTab(::mojom::LifecycleUnitDiscardReason::URGENT));
  ASSERT_TRUE(base::test::RunUntil([&] { return RowAt(index)->sleeping(); }));
  EXPECT_TRUE(RowAt(index)->sleep_badge()->GetVisible());
  EXPECT_TRUE(NameOf(RowAt(index)).ends_with(", asleep"));
}

IN_PROC_BROWSER_TEST_F(AerieSidebarSleepBrowserTest,
                       TheTodayHeaderCountsTheSleepingTabsAndSaysTheEstimateIsOne) {
  EXPECT_FALSE(sidebar()->sleeping_summary()->GetVisible());
  const int first = OpenBackgroundTab();
  const int second = OpenBackgroundTab();
  SleepTab(first);
  EXPECT_TRUE(sidebar()->sleeping_summary()->GetVisible());
  EXPECT_TRUE(sidebar()->sleeping_summary()->GetText().starts_with(u"1 asleep"));
  SleepTab(second);
  EXPECT_TRUE(sidebar()->sleeping_summary()->GetText().starts_with(u"2 asleep"));
  EXPECT_EQ(sidebar()->sleeping_summary()->GetTooltipText(),
            l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_SLEEPING_TOOLTIP));

  // Waking the tabs takes the text away again.
  strip()->ActivateTabAt(first);
  strip()->ActivateTabAt(second);
  EXPECT_TRUE(base::test::RunUntil(
      [&] { return !sidebar()->sleeping_summary()->GetVisible(); }));
}

IN_PROC_BROWSER_TEST_F(AerieSidebarSleepBrowserTest,
                       TheEstimateIsRoundedToTenMegabytesWhenThereIsOne) {
  const int index = OpenBackgroundTab();
  SleepTab(index);
  const std::u16string text(sidebar()->sleeping_summary()->GetText());
  const size_t about = text.find(u"about ");
  if (about == std::u16string::npos) {
    // Chromium had no footprint for the page: the count alone shows.
    EXPECT_EQ(text, u"1 asleep");
    return;
  }
  int megabytes = 0;
  ASSERT_TRUE(base::StringToInt(
      base::UTF16ToUTF8(text.substr(about + 6, text.find(u" MB") - about - 6)),
      &megabytes));
  EXPECT_EQ(megabytes % 10, 0);
  EXPECT_GT(megabytes, 0);
}

IN_PROC_BROWSER_TEST_F(AerieSidebarSleepBrowserTest,
                       TheContextMenuOfALoadedBackgroundTabOffersSleepTab) {
  const int index = OpenBackgroundTab();
  RowAt(index)->ShowContextMenu(gfx::Point(5, 5),
                                ui::mojom::MenuSourceType::kKeyboard);
  ASSERT_TRUE(sidebar()->context_menu());
  views::MenuItemView* item = sidebar()->context_menu()->sleep_item();
  ASSERT_TRUE(item);
  EXPECT_EQ(item->title(), l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_SLEEP_TAB));
  EXPECT_TRUE(sidebar()->context_menu()->IsCommandEnabled(
      AerieSidebarContextMenu::Command::kSleepTab));

  sidebar()->context_menu()->ExecuteCommand(
      AerieSidebarContextMenu::Command::kSleepTab);
  EXPECT_TRUE(base::test::RunUntil([&] { return RowAt(index)->sleeping(); }));
  sidebar()->context_menu()->Close();
}

IN_PROC_BROWSER_TEST_F(AerieSidebarSleepBrowserTest,
                       ATabThatCannotSleepHasTheItemDisabledWithTheReason) {
  // The selected tab.
  RowAt(0)->ShowContextMenu(gfx::Point(5, 5),
                            ui::mojom::MenuSourceType::kKeyboard);
  ASSERT_TRUE(sidebar()->context_menu());
  auto* menu = sidebar()->context_menu();
  EXPECT_FALSE(menu->IsCommandEnabled(AerieSidebarContextMenu::Command::kSleepTab));
  EXPECT_EQ(menu->GetTooltipText(AerieSidebarContextMenu::Command::kSleepTab,
                                 gfx::Point()),
            l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_SLEEP_TAB_SELECTED));
  // Choosing a disabled item does nothing: the tab keeps its row.
  menu->ExecuteCommand(AerieSidebarContextMenu::Command::kSleepTab);
  EXPECT_FALSE(RowAt(0)->sleeping());
  EXPECT_FALSE(strip()->GetWebContentsAt(0)->WasDiscarded());
  menu->Close();
}

IN_PROC_BROWSER_TEST_F(AerieSidebarSleepBrowserTest,
                       ASleepingTabHasTheItemDisabledWithItsOwnReason) {
  const int index = OpenBackgroundTab();
  SleepTab(index);
  RowAt(index)->ShowContextMenu(gfx::Point(5, 5),
                                ui::mojom::MenuSourceType::kKeyboard);
  ASSERT_TRUE(sidebar()->context_menu());
  auto* menu = sidebar()->context_menu();
  EXPECT_FALSE(menu->IsCommandEnabled(AerieSidebarContextMenu::Command::kSleepTab));
  EXPECT_EQ(menu->GetTooltipText(AerieSidebarContextMenu::Command::kSleepTab,
                                 gfx::Point()),
            l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_SLEEP_TAB_ASLEEP));
  menu->Close();
}
