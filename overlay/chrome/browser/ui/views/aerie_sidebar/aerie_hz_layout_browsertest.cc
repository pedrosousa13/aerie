// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

// Browser tests of the horizontal tab layout (issue #186): the layout pref
// switches between the sidebar and Chromium's horizontal strip without a
// restart and without touching the tabs; the strip's order mirrors the Tab
// Model and a reorder in the strip is written back; the strip keeps the tablist
// and tab roles and names sleeping and renamed tabs like the sidebar.

#include <string>
#include <vector>

#include "base/run_loop.h"
#include "base/test/run_until.h"
#include "chrome/browser/aerie_tab_model/horizontal_order_mirror.h"
#include "chrome/browser/aerie_tab_model/sleep_controller.h"
#include "chrome/browser/aerie_tab_model/sleep_controller_factory.h"
#include "chrome/browser/aerie_tab_model/tab_model_service.h"
#include "chrome/browser/aerie_tab_model/tab_model_service_factory.h"
#include "chrome/browser/aerie_tab_model/tab_strip_adapter.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_row_view.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_view.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/tab_strip_region_view.h"
#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/aerie_sidebar/aerie_sidebar_layout.h"
#include "components/aerie_sidebar/aerie_sidebar_prefs.h"
#include "components/aerie_tab_model/tab_model.h"
#include "components/prefs/pref_service.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/gfx/animation/animation_test_api.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/label.h"
#include "ui/views/test/views_test_utils.h"
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

void CollectByRole(views::View* root,
                   ax::mojom::Role role,
                   std::vector<views::View*>* out) {
  ui::AXNodeData data;
  root->GetViewAccessibility().GetAccessibleNodeData(&data);
  if (data.role == role) {
    out->push_back(root);
  }
  for (views::View* child : root->children()) {
    CollectByRole(child, role, out);
  }
}

std::string NameOf(views::View* view) {
  ui::AXNodeData data;
  view->GetViewAccessibility().GetAccessibleNodeData(&data);
  return data.GetStringAttribute(ax::mojom::StringAttribute::kName);
}

}  // namespace

class AerieHzLayoutBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    host_resolver()->AddRule("*", "127.0.0.1");
    ASSERT_TRUE(embedded_test_server()->Start());
    // The instant look: motion is tested in the interactive tests.
    reduced_motion_.emplace(
        gfx::AnimationTestApi::SetPrefersReducedMotionForTesting(true));
    browser_view()->GetWidget()->SetBounds(gfx::Rect(0, 0, 1100, 750));
    Settle();
  }

  void TearDownOnMainThread() override {
    reduced_motion_.reset();
    InProcessBrowserTest::TearDownOnMainThread();
  }

  BrowserView* browser_view() {
    return BrowserView::GetBrowserViewForBrowser(browser());
  }
  VerticalTabStripRegionView* sidebar_region() {
    return browser_view()->vertical_tab_strip_region_view_for_testing();
  }
  AerieSidebarView* sidebar() {
    return FindDescendant<AerieSidebarView>(sidebar_region());
  }
  // The region that holds the tab strip now: the horizontal one, or the
  // sidebar's.
  TabStripRegionView* strip_region() { return browser_view()->tab_strip_view(); }
  bool StripIsHorizontal() {
    return static_cast<views::View*>(strip_region()) !=
           static_cast<views::View*>(sidebar_region());
  }
  PrefService* prefs() { return browser()->GetProfile()->GetPrefs(); }
  TabStripModel* strip() { return browser()->tab_strip_model(); }
  aerie_tab_model::TabModelService* service() {
    return aerie_tab_model::TabModelServiceFactory::GetForProfile(
        browser()->GetProfile());
  }

  void Settle() {
    base::RunLoop().RunUntilIdle();
    views::test::RunScheduledLayout(browser_view());
  }

  void Choose(const char* layout) {
    prefs()->SetString(aerie_sidebar::kLayoutPref, layout);
    Settle();
    service()->horizontal_order_mirror()->FlushForTesting();
    Settle();
  }

  void OpenTab(const char* path = "/title1.html") {
    ASSERT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
        browser(), embedded_test_server()->GetURL(path),
        WindowOpenDisposition::NEW_BACKGROUND_TAB,
        ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
  }

  std::string RecordId(int index) {
    return service()->adapter()->RecordIdForTab(strip()->GetTabAtIndex(index));
  }
  std::vector<std::string> StripIds() {
    std::vector<std::string> ids;
    for (int i = 0; i < strip()->count(); ++i) {
      ids.push_back(RecordId(i));
    }
    return ids;
  }
  std::vector<views::View*> StripTabViews() {
    std::vector<views::View*> tabs;
    CollectByRole(strip_region(), ax::mojom::Role::kTab, &tabs);
    return tabs;
  }

 private:
  std::optional<gfx::AnimationTestApi::PrefersReducedMotionResetter>
      reduced_motion_;
};

IN_PROC_BROWSER_TEST_F(AerieHzLayoutBrowserTest,
                       TheDefaultLayoutIsTheSidebarAndTheStripIsNotBuilt) {
  EXPECT_EQ(prefs()->GetString(aerie_sidebar::kLayoutPref),
            aerie_sidebar::kLayoutSidebar);
  EXPECT_FALSE(StripIsHorizontal());
  ASSERT_TRUE(sidebar());
  EXPECT_TRUE(sidebar_region()->GetVisible());
}

IN_PROC_BROWSER_TEST_F(AerieHzLayoutBrowserTest,
                       SwitchingLayoutShowsTheStripAndHidesTheSidebarAndBack) {
  OpenTab();
  OpenTab();
  strip()->ActivateTabAt(1);
  std::vector<content::WebContents*> contents;
  for (int i = 0; i < strip()->count(); ++i) {
    contents.push_back(strip()->GetWebContentsAt(i));
  }
  const std::vector<std::string> ids = StripIds();

  for (const char* layout : {aerie_sidebar::kLayoutHorizontal,
                             aerie_sidebar::kLayoutHorizontalOneRow}) {
    Choose(layout);
    EXPECT_TRUE(StripIsHorizontal()) << layout;
    EXPECT_FALSE(sidebar_region()->GetVisible()) << layout;
    EXPECT_EQ(sidebar(), nullptr) << layout;
    // The tabs did not change.
    ASSERT_EQ(strip()->count(), 3);
    EXPECT_EQ(strip()->active_index(), 1);
    for (int i = 0; i < 3; ++i) {
      EXPECT_EQ(strip()->GetWebContentsAt(i), contents[i]);
    }
    EXPECT_EQ(StripIds(), ids);
    EXPECT_EQ(StripTabViews().size(), 3u);
  }

  Choose(aerie_sidebar::kLayoutSidebar);
  EXPECT_FALSE(StripIsHorizontal());
  EXPECT_TRUE(sidebar_region()->GetVisible());
  ASSERT_TRUE(sidebar());
  ASSERT_TRUE(base::test::RunUntil([&] { return sidebar()->row_count() == 3u; }));
  EXPECT_EQ(strip()->active_index(), 1);
  for (int i = 0; i < 3; ++i) {
    EXPECT_EQ(strip()->GetWebContentsAt(i), contents[i]);
  }
}

IN_PROC_BROWSER_TEST_F(AerieHzLayoutBrowserTest,
                       TheLayoutIsKeptInThePrefAndAnUnknownValueIsTheSidebar) {
  Choose(aerie_sidebar::kLayoutHorizontalOneRow);
  EXPECT_EQ(aerie_sidebar::GetTabLayout(*prefs()),
            aerie_sidebar::TabLayout::kHorizontalOneRow);
  Choose("nonsense");
  EXPECT_FALSE(StripIsHorizontal());
  EXPECT_NE(sidebar(), nullptr);
}

IN_PROC_BROWSER_TEST_F(AerieHzLayoutBrowserTest,
                       TheStripFollowsTheModelsOrderPinnedFirst) {
  OpenTab();
  OpenTab();
  ASSERT_EQ(strip()->count(), 3);
  const std::vector<std::string> ids = StripIds();
  for (const auto& id : ids) {
    ASSERT_FALSE(id.empty());
  }
  // The last tab becomes Pinned in the model.
  ASSERT_TRUE(service()->model()->MoveTab(
      ids[2], aerie_tab_model::Section::kPinned, "", 0));
  Choose(aerie_sidebar::kLayoutHorizontal);
  EXPECT_EQ(StripIds(), (std::vector<std::string>{ids[2], ids[0], ids[1]}));
  // Nothing was recreated.
  EXPECT_EQ(strip()->count(), 3);
}

IN_PROC_BROWSER_TEST_F(AerieHzLayoutBrowserTest,
                       AReorderInTheStripIsWrittenBackAndShowsInTheSidebar) {
  OpenTab("/title2.html");
  OpenTab("/title3.html");
  const std::vector<std::string> ids = StripIds();
  Choose(aerie_sidebar::kLayoutHorizontal);
  // The user drags the first tab to the end.
  strip()->MoveWebContentsAt(0, 2, /*select_after_move=*/false);
  service()->horizontal_order_mirror()->FlushForTesting();
  Settle();
  const std::vector<std::string> want = {ids[1], ids[2], ids[0]};
  EXPECT_EQ(StripIds(), want);
  // The model holds it: Today follows the strip.
  EXPECT_LT(service()->model()->GetTab(ids[1])->position,
            service()->model()->GetTab(ids[2])->position);
  EXPECT_LT(service()->model()->GetTab(ids[2])->position,
            service()->model()->GetTab(ids[0])->position);

  Choose(aerie_sidebar::kLayoutSidebar);
  ASSERT_TRUE(base::test::RunUntil([&] { return sidebar()->row_count() == 3u; }));
  EXPECT_EQ(StripIds(), want);
  // The sidebar's rows show the pages in the strip's order.
  for (size_t i = 0; i < 3; ++i) {
    EXPECT_EQ(sidebar()->row_at(i)->title_view()->GetText(),
              strip()->GetWebContentsAt(static_cast<int>(i))->GetTitle());
  }
}

IN_PROC_BROWSER_TEST_F(AerieHzLayoutBrowserTest,
                       TheStripKeepsTheTablistAndTabRoles) {
  OpenTab();
  Choose(aerie_sidebar::kLayoutHorizontal);
  std::vector<views::View*> lists;
  CollectByRole(strip_region(), ax::mojom::Role::kTabList, &lists);
  EXPECT_GE(lists.size(), 1u);
  EXPECT_EQ(StripTabViews().size(), 2u);
}

IN_PROC_BROWSER_TEST_F(AerieHzLayoutBrowserTest,
                       ARenamedAndASleepingTabAreNamedLikeTheSidebarRows) {
  OpenTab();
  OpenTab();
  const std::string renamed_id = RecordId(1);
  ASSERT_TRUE(service()->model()->UpdateTab(
      renamed_id, [](aerie_tab_model::TabRecord& t) { t.manual_name = "Docs"; }));
  // The sidebar's name for the same tab, to compare with.
  ASSERT_TRUE(base::test::RunUntil([&] { return sidebar()->row_count() == 3u; }));
  const std::string sidebar_renamed = NameOf(sidebar()->row_at(1));
  EXPECT_EQ(sidebar_renamed, "Docs, renamed");

  auto* sleep = aerie_tab_model::SleepControllerFactory::GetForProfile(
      browser()->GetProfile());
  ASSERT_TRUE(sleep);
  ASSERT_TRUE(sleep->Sleep(strip()->GetTabAtIndex(2)));
  ASSERT_TRUE(base::test::RunUntil([&] { return sidebar()->row_at(2)->sleeping(); }));
  const std::string sidebar_asleep = NameOf(sidebar()->row_at(2));
  EXPECT_TRUE(sidebar_asleep.ends_with(", asleep"));

  Choose(aerie_sidebar::kLayoutHorizontal);
  ASSERT_EQ(StripTabViews().size(), 3u);
  // Re-read the names: a layout switch builds the tab views fresh.
  EXPECT_EQ(NameOf(StripTabViews()[1]), sidebar_renamed);
  EXPECT_EQ(NameOf(StripTabViews()[2]), sidebar_asleep);
}
