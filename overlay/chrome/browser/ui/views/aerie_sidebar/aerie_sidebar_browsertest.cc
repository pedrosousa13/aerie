// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

// Browser tests of the Aerie sidebar (issue #177): the Today list follows the
// tabs, Ctrl+S shows and hides it with the pref as its memory, a resize is
// clamped and stored, reduced motion never animates the transform, and the
// accessibility tree has the roles the design asks for.

#include <string>

#include "base/functional/bind.h"
#include "base/run_loop.h"
#include "base/task/single_thread_task_runner.h"
#include "base/time/time.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_row_view.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_view.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/aerie_sidebar/aerie_sidebar_layout.h"
#include "components/aerie_sidebar/aerie_sidebar_prefs.h"
#include "components/prefs/pref_service.h"
#include "content/public/test/browser_test.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/compositor/layer.h"
#include "ui/events/event.h"
#include "ui/gfx/animation/animation_test_api.h"
#include "ui/gfx/geometry/point.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/resize_area.h"
#include "ui/views/test/button_test_api.h"
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

ax::mojom::Role RoleOf(views::View* view) {
  ui::AXNodeData data;
  view->GetViewAccessibility().GetAccessibleNodeData(&data);
  return data.role;
}

std::string NameOf(views::View* view) {
  ui::AXNodeData data;
  view->GetViewAccessibility().GetAccessibleNodeData(&data);
  return data.GetStringAttribute(ax::mojom::StringAttribute::kName);
}

}  // namespace

class AerieSidebarBrowserTest : public InProcessBrowserTest {
 protected:
  VerticalTabStripRegionView* region() {
    return BrowserView::GetBrowserViewForBrowser(browser())
        ->vertical_tab_strip_region_view_for_testing();
  }
  AerieSidebarView* sidebar() { return FindDescendant<AerieSidebarView>(region()); }
  PrefService* prefs() { return browser()->GetProfile()->GetPrefs(); }

  // Runs the message loop until `done` is true, and fails after 10 s of loop.
  template <typename Predicate>
  bool WaitFor(Predicate done) {
    const base::TimeTicks deadline = base::TimeTicks::Now() + base::Seconds(10);
    while (!done() && base::TimeTicks::Now() < deadline) {
      base::RunLoop loop;
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
          FROM_HERE, loop.QuitClosure(), base::Milliseconds(20));
      loop.Run();
    }
    return done();
  }
};

IN_PROC_BROWSER_TEST_F(AerieSidebarBrowserTest, TodayListMatchesTheTabs) {
  ASSERT_TRUE(region());
  ASSERT_TRUE(sidebar());
  EXPECT_TRUE(WaitFor([&] { return sidebar()->row_count() == 1u; }));

  chrome::ExecuteCommand(browser(), IDC_NEW_TAB);
  chrome::ExecuteCommand(browser(), IDC_NEW_TAB);
  EXPECT_EQ(browser()->tab_strip_model()->count(), 3);
  EXPECT_TRUE(WaitFor([&] { return sidebar()->row_count() == 3u; }));

  browser()->tab_strip_model()->CloseWebContentsAt(
      2, TabCloseTypes::CLOSE_NONE);
  EXPECT_TRUE(WaitFor([&] { return sidebar()->row_count() == 2u; }));
}

IN_PROC_BROWSER_TEST_F(AerieSidebarBrowserTest, CtrlSHidesAndShowsAndRemembers) {
  auto reduced = gfx::AnimationTestApi::SetPrefersReducedMotionForTesting(true);
  ASSERT_TRUE(region());
  EXPECT_TRUE(aerie_sidebar::IsVisible(*prefs()));
  EXPECT_FALSE(region()->aerie_sidebar_hidden());

  chrome::ExecuteCommand(browser(), IDC_CTRL_S_SHORTCUT);
  EXPECT_FALSE(aerie_sidebar::IsVisible(*prefs()));
  EXPECT_TRUE(WaitFor([&] { return region()->aerie_sidebar_hidden(); }));
  EXPECT_EQ(region()->GetPreferredSize().width(), 0);

  chrome::ExecuteCommand(browser(), IDC_CTRL_S_SHORTCUT);
  EXPECT_TRUE(aerie_sidebar::IsVisible(*prefs()));
  EXPECT_FALSE(region()->aerie_sidebar_hidden());
  EXPECT_GT(region()->GetPreferredSize().width(), 0);
}

IN_PROC_BROWSER_TEST_F(AerieSidebarBrowserTest, ReducedMotionNeverTransformsTheLayer) {
  ASSERT_TRUE(region());
  {
    auto reset = gfx::AnimationTestApi::SetPrefersReducedMotionForTesting(true);
    chrome::ExecuteCommand(browser(), IDC_CTRL_S_SHORTCUT);
    EXPECT_TRUE(region()->layer()->GetTargetTransform().IsIdentity());
    EXPECT_TRUE(WaitFor([&] { return region()->aerie_sidebar_hidden(); }));
    chrome::ExecuteCommand(browser(), IDC_CTRL_S_SHORTCUT);
    EXPECT_TRUE(region()->layer()->GetTargetTransform().IsIdentity());
  }
  {
    auto reset = gfx::AnimationTestApi::SetPrefersReducedMotionForTesting(false);
    chrome::ExecuteCommand(browser(), IDC_CTRL_S_SHORTCUT);
    // Full motion slides the layer out of view.
    EXPECT_FALSE(region()->layer()->GetTargetTransform().IsIdentity());
  }
}

IN_PROC_BROWSER_TEST_F(AerieSidebarBrowserTest, ResizeIsClampedAndStored) {
  ASSERT_TRUE(region());
  // The test window may be narrower than the stored width, so a drag starts
  // from what the column really is.
  const int start = region()->width();
  region()->OnResize(30, /*done_resizing=*/true);
  EXPECT_EQ(prefs()->GetInteger(aerie_sidebar::kWidthPref),
            aerie_sidebar::ClampWidth(start + 30));
  region()->OnResize(5000, /*done_resizing=*/true);
  EXPECT_EQ(prefs()->GetInteger(aerie_sidebar::kWidthPref), 400);
  region()->OnResize(-5000, /*done_resizing=*/true);
  EXPECT_EQ(prefs()->GetInteger(aerie_sidebar::kWidthPref), 200);
  EXPECT_EQ(region()->resize_area_for_testing()->GetViewAccessibility().GetCachedName(),
            u"Resize sidebar");
}

IN_PROC_BROWSER_TEST_F(AerieSidebarBrowserTest, AccessibilityTreeHasTheDesignRoles) {
  ASSERT_TRUE(sidebar());
  EXPECT_TRUE(WaitFor([&] { return sidebar()->row_count() == 1u; }));
  EXPECT_EQ(RoleOf(sidebar()), ax::mojom::Role::kNavigation);
  EXPECT_EQ(NameOf(sidebar()), "Sidebar");
  auto* row = FindDescendant<AerieSidebarRowView>(sidebar());
  ASSERT_TRUE(row);
  EXPECT_EQ(RoleOf(row), ax::mojom::Role::kTreeItem);
  EXPECT_EQ(RoleOf(row->parent()), ax::mojom::Role::kTree);
}

IN_PROC_BROWSER_TEST_F(AerieSidebarBrowserTest, TheCloseButtonClosesTheTabAndTheRowKeepsItShownUnderThePointer) {
  chrome::ExecuteCommand(browser(), IDC_NEW_TAB);
  EXPECT_TRUE(WaitFor([&] { return sidebar()->row_count() == 2u; }));
  auto* row = FindDescendant<AerieSidebarRowView>(sidebar());
  ASSERT_TRUE(row);
  // Moving onto the close button must not count as leaving the row.
  EXPECT_TRUE(row->GetNotifyEnterExitOnChild());
  auto* close = FindDescendant<views::ImageButton>(row);
  ASSERT_TRUE(close);
  ui::MouseEvent entered(ui::EventType::kMouseEntered, gfx::Point(), gfx::Point(),
                         base::TimeTicks::Now(), 0, 0);
  row->OnMouseEntered(entered);
  EXPECT_TRUE(close->GetVisible());

  const int tabs = browser()->tab_strip_model()->count();
  ui::MouseEvent click(ui::EventType::kMouseReleased, gfx::Point(1, 1), gfx::Point(1, 1),
                       base::TimeTicks::Now(), ui::EF_LEFT_MOUSE_BUTTON,
                       ui::EF_LEFT_MOUSE_BUTTON);
  views::test::ButtonTestApi(close).NotifyClick(click);
  EXPECT_TRUE(WaitFor([&] { return browser()->tab_strip_model()->count() == tabs - 1; }));
  EXPECT_TRUE(WaitFor([&] { return sidebar()->row_count() == 1u; }));
}

IN_PROC_BROWSER_TEST_F(AerieSidebarBrowserTest, TwoQuickCtrlSKeepThePrefAndTheViewInStep) {
  auto reduced = gfx::AnimationTestApi::SetPrefersReducedMotionForTesting(false);
  ASSERT_TRUE(region());
  // Full motion: a hide leaves the layout only when its slide has ended, and
  // the second toggle comes before that.
  chrome::ExecuteCommand(browser(), IDC_CTRL_S_SHORTCUT);
  chrome::ExecuteCommand(browser(), IDC_CTRL_S_SHORTCUT);
  EXPECT_TRUE(aerie_sidebar::IsVisible(*prefs()));
  EXPECT_TRUE(region()->aerie_sidebar_target_visible());
  // Wait past the slide (140 ms) and check the view did not hide.
  const base::TimeTicks until = base::TimeTicks::Now() + base::Milliseconds(600);
  WaitFor([&] { return base::TimeTicks::Now() >= until; });
  EXPECT_FALSE(region()->aerie_sidebar_hidden());
  EXPECT_TRUE(region()->GetVisible());
  EXPECT_GT(region()->GetPreferredSize().width(), 0);

  // And the other way round: two toggles from hidden.
  chrome::ExecuteCommand(browser(), IDC_CTRL_S_SHORTCUT);
  EXPECT_TRUE(WaitFor([&] { return region()->aerie_sidebar_hidden(); }));
  chrome::ExecuteCommand(browser(), IDC_CTRL_S_SHORTCUT);
  chrome::ExecuteCommand(browser(), IDC_CTRL_S_SHORTCUT);
  EXPECT_FALSE(aerie_sidebar::IsVisible(*prefs()));
  EXPECT_TRUE(WaitFor([&] { return region()->aerie_sidebar_hidden(); }));
}
