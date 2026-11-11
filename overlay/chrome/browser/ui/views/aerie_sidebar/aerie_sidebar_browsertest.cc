// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

// Browser tests of the Aerie sidebar (issue #177): the Today list follows the
// tabs, Ctrl+S shows and hides it with the pref as its memory, a resize is
// clamped and stored, reduced motion never animates the transform, and the
// accessibility tree has the roles the design asks for.

#include <algorithm>
#include <string>

#include "base/functional/bind.h"
#include "base/run_loop.h"
#include "base/strings/string_number_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "base/time/time.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/view_ids.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_regions.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_row_view.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_top_row.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_view.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/multi_contents_view.h"
#include "chrome/browser/ui/views/frame/top_container_view.h"
#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"
#include "chrome/browser/ui/views/location_bar/location_bar_view.h"
#include "chrome/browser/ui/views/toolbar/toolbar_view.h"
#include "chrome/common/pref_names.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/aerie_adblock/browser/guard_state.h"
#include "components/aerie_adblock/browser/page_block_counter.h"
#include "components/aerie_sidebar/aerie_sidebar_layout.h"
#include "components/aerie_sidebar/aerie_sidebar_prefs.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/dns/mock_host_resolver.h"
#include "url/gurl.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/compositor/layer.h"
#include "ui/events/event.h"
#include "ui/gfx/animation/animation_test_api.h"
#include "ui/gfx/geometry/point.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/focus_ring.h"
#include "ui/views/controls/resize_area.h"
#include "ui/views/test/button_test_api.h"
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


// Issue #215, items 1 and 2: the top row, the address pill and the bottom bar.

IN_PROC_BROWSER_TEST_F(AerieSidebarBrowserTest, TheRegionsAreInTheDesignOrder) {
  ASSERT_TRUE(sidebar());
  ASSERT_TRUE(sidebar()->top_row());
  ASSERT_TRUE(sidebar()->address_pill());
  ASSERT_TRUE(sidebar()->bottom_bar());
  const auto& children = sidebar()->children();
  auto index_of = [&](views::View* view) {
    return std::find(children.begin(), children.end(), view) - children.begin();
  };
  EXPECT_LT(index_of(sidebar()->top_row()), index_of(sidebar()->address_pill()));
  EXPECT_LT(index_of(sidebar()->address_pill()),
            index_of(sidebar()->bottom_bar()));
  EXPECT_EQ(index_of(sidebar()->top_row()), 0);
  EXPECT_EQ(index_of(sidebar()->bottom_bar()),
            static_cast<ptrdiff_t>(children.size()) - 1);
}

IN_PROC_BROWSER_TEST_F(AerieSidebarBrowserTest, TheTopRowHasToggleBackForwardReload) {
  AerieSidebarTopRow* row = sidebar()->top_row();
  ASSERT_TRUE(row);
  ASSERT_TRUE(row->toggle_button());
  EXPECT_EQ(NameOf(row->toggle_button()), "Hide sidebar");
  EXPECT_EQ(RoleOf(row), ax::mojom::Role::kToolbar);

  // Back, Forward and Reload are the toolbar's own buttons, in this order.
  ASSERT_EQ(row->navigation()->children().size(), 3u);
  EXPECT_EQ(row->navigation()->children()[0]->GetID(), VIEW_ID_BACK_BUTTON);
  EXPECT_EQ(row->navigation()->children()[1]->GetID(), VIEW_ID_FORWARD_BUTTON);
  EXPECT_EQ(row->navigation()->children()[2]->GetID(), VIEW_ID_RELOAD_BUTTON);
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  EXPECT_FALSE(browser_view->top_container()->Contains(
      row->navigation()->children()[2]));
  // The toggle comes before the navigation buttons.
  const auto& children = row->children();
  EXPECT_LT(std::find(children.begin(), children.end(), row->toggle_button()),
            std::find(children.begin(), children.end(), row->navigation()));
}

IN_PROC_BROWSER_TEST_F(AerieSidebarBrowserTest, TheToggleButtonHidesTheSidebar) {
  auto reduced = gfx::AnimationTestApi::SetPrefersReducedMotionForTesting(true);
  ASSERT_TRUE(sidebar());
  EXPECT_TRUE(aerie_sidebar::IsVisible(*prefs()));
  ui::MouseEvent click(ui::EventType::kMouseReleased, gfx::Point(1, 1),
                       gfx::Point(1, 1), base::TimeTicks::Now(),
                       ui::EF_LEFT_MOUSE_BUTTON, ui::EF_LEFT_MOUSE_BUTTON);
  views::test::ButtonTestApi(
      views::AsViewClass<views::ImageButton>(sidebar()->top_row()->toggle_button()))
      .NotifyClick(click);
  EXPECT_FALSE(aerie_sidebar::IsVisible(*prefs()));
  EXPECT_TRUE(WaitFor([&] { return region()->aerie_sidebar_hidden(); }));
}

IN_PROC_BROWSER_TEST_F(AerieSidebarBrowserTest, TheEmptyPartOfTheTopRowIsTheWindowDragHandle) {
  AerieSidebarTopRow* row = sidebar()->top_row();
  ASSERT_TRUE(row);
  views::test::RunScheduledLayout(region()->GetWidget());
  // A point on the toggle button is not caption; a point between the toggle and
  // the navigation buttons is. Both are tested in the row's coordinates and
  // then through the region view that Helium's hit test asks.
  const gfx::Rect toggle = row->toggle_button()->bounds();
  const gfx::Rect navigation = row->navigation()->bounds();
  ASSERT_LT(toggle.right(), navigation.x());
  const gfx::Point on_toggle = toggle.CenterPoint();
  const gfx::Point gap((toggle.right() + navigation.x()) / 2, row->height() / 2);
  EXPECT_FALSE(row->IsPositionInWindowCaption(on_toggle));
  EXPECT_TRUE(row->IsPositionInWindowCaption(gap));

  auto to_region = [&](gfx::Point p) {
    views::View::ConvertPointToTarget(row, region(), &p);
    return p;
  };
  EXPECT_TRUE(region()->IsPositionInWindowCaption(to_region(gap)));
  EXPECT_FALSE(region()->IsPositionInWindowCaption(to_region(on_toggle)));
  // Below the top row is the tab list, which is never caption.
  // Below the top row is the pill and the tab list, never caption (asked of
  // the sidebar, because a headless test window is too small to hold them).
  EXPECT_FALSE(sidebar()->IsPositionInWindowCaption(
      gfx::Point(10, row->bounds().bottom() + 4)));
}

IN_PROC_BROWSER_TEST_F(AerieSidebarBrowserTest, TheTopRowDrawsWindowControlsWhenAsked) {
  AerieSidebarTopRow* row = sidebar()->top_row();
  ASSERT_TRUE(row);
  sidebar()->SetWindowControls(/*leading_inset=*/0, /*draw_window_controls=*/true);
  EXPECT_TRUE(row->window_controls_shown());
  ASSERT_EQ(row->window_controls()->children().size(), 3u);
  EXPECT_EQ(NameOf(row->window_controls()->children()[0]), "Close window");
  EXPECT_EQ(NameOf(row->window_controls()->children()[1]), "Minimize");
  EXPECT_EQ(NameOf(row->window_controls()->children()[2]), "Maximize");
  // A control is not a Tab stop, but a screen reader reaches it.
  EXPECT_EQ(row->window_controls()->children()[0]->GetFocusBehavior(),
            views::View::FocusBehavior::ACCESSIBLE_ONLY);

  // Room for the platform's own controls pushes the row's content aside.
  sidebar()->SetWindowControls(/*leading_inset=*/78, /*draw_window_controls=*/false);
  EXPECT_FALSE(row->window_controls_shown());
  views::test::RunScheduledLayout(sidebar());
  EXPECT_GE(row->toggle_button()->bounds().x(), 78 - 8);
}

// The pill needs a page the Guard icon shows on: an http page.
class AerieSidebarPillBrowserTest : public AerieSidebarBrowserTest {
 public:
  void SetUpOnMainThread() override {
    AerieSidebarBrowserTest::SetUpOnMainThread();
    // HTTPS-only mode is on by default in Aerie and would upgrade the page.
    prefs()->SetBoolean(prefs::kHttpsOnlyModeEnabled, false);
    host_resolver()->AddRule("*", "127.0.0.1");
    embedded_test_server()->ServeFilesFromSourceDirectory("content/test/data");
    ASSERT_TRUE(embedded_test_server()->Start());
  }

  BrowserView* browser_view() {
    return BrowserView::GetBrowserViewForBrowser(browser());
  }

  // The visible view under the pill whose accessible name starts with
  // "Guard:", which is how the Guard icon names itself.
  views::View* GuardIcon(views::View* root) {
    ui::AXNodeData data;
    root->GetViewAccessibility().GetAccessibleNodeData(&data);
    if (root->GetVisible() &&
        data.GetStringAttribute(ax::mojom::StringAttribute::kName)
                .rfind("Guard:", 0) == 0) {
      return root;
    }
    for (views::View* child : root->children()) {
      if (views::View* found = GuardIcon(child)) {
        return found;
      }
    }
    return nullptr;
  }

  void OpenPage() {
    ASSERT_TRUE(content::NavigateToURL(
        browser()->tab_strip_model()->GetActiveWebContents(),
        embedded_test_server()->GetURL("news.test", "/title1.html")));
  }
};

IN_PROC_BROWSER_TEST_F(AerieSidebarPillBrowserTest, TheAddressPillIsTheRealLocationBarAndTheToolbarRowIsGone) {
  ASSERT_TRUE(sidebar());
  LocationBarView* location_bar = browser_view()->GetLocationBarView();
  ASSERT_TRUE(location_bar);
  EXPECT_EQ(location_bar->parent(), sidebar()->address_pill());
  EXPECT_TRUE(sidebar()->Contains(location_bar));
  EXPECT_EQ(browser_view()->toolbar()->location_bar_view(), location_bar);

  // What is left of the toolbar is in the bottom bar, not above the page.
  EXPECT_EQ(browser_view()->toolbar()->parent(), sidebar()->bottom_bar());
  EXPECT_FALSE(browser_view()->top_container()->Contains(browser_view()->toolbar()));
  views::test::RunScheduledLayout(browser_view());
  EXPECT_EQ(browser_view()->top_container()->height(), 0);
  // The sidebar's column starts at the top of the window.
  EXPECT_EQ(region()->y(), 0);
}

IN_PROC_BROWSER_TEST_F(AerieSidebarPillBrowserTest, TheGuardIconIsInThePillWithItsName) {
  OpenPage();
  views::View* pill = sidebar()->address_pill();
  views::View* icon = nullptr;
  ASSERT_TRUE(WaitFor([&] {
    icon = GuardIcon(pill);
    return icon != nullptr;
  }));
  EXPECT_TRUE(pill->Contains(icon));
  // Nothing was blocked on this page, and the name still says the count.
  EXPECT_EQ(NameOf(icon), "Guard: 0 blocked on this page");
}

// The page actions have to fit when the sidebar is as narrow as it goes
// (size-sidebar-min, 200 px, less the 8 px inset on both sides). The Guard
// badge is at its widest with a count above 99, which it shows as "99+".
IN_PROC_BROWSER_TEST_F(AerieSidebarPillBrowserTest, ThePageActionsFitAtTwoHundredPixels) {
  OpenPage();
  LocationBarView* location_bar = browser_view()->GetLocationBarView();
  views::View* icon = nullptr;
  ASSERT_TRUE(WaitFor([&] {
    icon = GuardIcon(sidebar()->address_pill());
    return icon != nullptr;
  }));

  auto expect_the_actions_fit = [&](const std::string& when) {
    SCOPED_TRACE(when);
    views::test::RunScheduledLayout(location_bar->GetWidget());
    const int width = 200 - 2 * 8;
    location_bar->SetSize(
        gfx::Size(width, location_bar->GetPreferredSize().height()));
    views::test::RunScheduledLayout(location_bar);

    const gfx::Rect local = location_bar->GetLocalBounds();
    gfx::Rect icon_in_bar = icon->GetLocalBounds();
    icon_in_bar =
        views::View::ConvertRectToTarget(icon, location_bar, icon_in_bar);
    EXPECT_TRUE(icon->GetVisible());
    EXPECT_FALSE(icon_in_bar.IsEmpty());
    EXPECT_TRUE(local.Contains(icon_in_bar))
        << "the Guard icon " << icon_in_bar.ToString()
        << " is outside the pill " << local.ToString();

    // The page actions do not push the text field out: the omnibox keeps room
    // to type, and the actions sit after it (toward the pill's end), never over
    // it.
    views::View* omnibox = location_bar->GetViewByID(VIEW_ID_OMNIBOX);
    ASSERT_TRUE(omnibox);
    gfx::Rect omnibox_in_bar = omnibox->GetLocalBounds();
    omnibox_in_bar =
        views::View::ConvertRectToTarget(omnibox, location_bar, omnibox_in_bar);
    EXPECT_GE(omnibox_in_bar.width(), 40);
    EXPECT_FALSE(omnibox_in_bar.Intersects(icon_in_bar));
  };

  // No badge: nothing was blocked on this page.
  expect_the_actions_fit("blocked count 0");

  // The widest badge: 100 blocked requests, shown as "99+".
  ASSERT_EQ(aerie_adblock::GuardBadgeLabel(100, /*show_count=*/true), u"99+");
  content::WebContents* contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  for (int i = 0; i < 100; ++i) {
    aerie_adblock::PageBlockCounter::RecordBlocked(
        contents->GetPrimaryMainFrame()->GetGlobalId(),
        GURL("https://ads.test/ad" + base::NumberToString(i) + ".js"));
  }
  ASSERT_TRUE(WaitFor([&] {
    views::View* current = GuardIcon(sidebar()->address_pill());
    return current && NameOf(current) == "Guard: 100 blocked on this page";
  }));
  icon = GuardIcon(sidebar()->address_pill());
  ASSERT_TRUE(icon);
  expect_the_actions_fit("blocked count 100, badge 99+");
}


// Issue #215, item 3: the keyboard and the accessibility tree.

IN_PROC_BROWSER_TEST_F(AerieSidebarBrowserTest, TheTodayListIsOneRovingTabStop) {
  chrome::ExecuteCommand(browser(), IDC_NEW_TAB);
  chrome::ExecuteCommand(browser(), IDC_NEW_TAB);
  ASSERT_TRUE(WaitFor([&] { return sidebar()->row_count() == 3u; }));

  // Exactly one row is reached with Tab: the selected one. The others are
  // reached with the arrow keys, which move the stop, and by a screen reader.
  int stops = 0;
  for (size_t i = 0; i < sidebar()->row_count(); ++i) {
    AerieSidebarRowView* row = sidebar()->row_at(i);
    if (row->GetFocusBehavior() == views::View::FocusBehavior::ALWAYS) {
      ++stops;
      EXPECT_TRUE(row->selected());
      EXPECT_EQ(row, sidebar()->tab_stop_row());
    } else {
      EXPECT_EQ(row->GetFocusBehavior(),
                views::View::FocusBehavior::ACCESSIBLE_ONLY);
    }
  }
  EXPECT_EQ(stops, 1);

  // Selecting another tab moves the stop with the selection.
  browser()->tab_strip_model()->ActivateTabAt(0);
  EXPECT_TRUE(WaitFor([&] {
    return sidebar()->tab_stop_row() == sidebar()->row_at(0);
  }));
}

IN_PROC_BROWSER_TEST_F(AerieSidebarBrowserTest, TheFocusRingIsTwoPixelsWithTwoPixelOffset) {
  ASSERT_TRUE(WaitFor([&] { return sidebar()->row_count() == 1u; }));
  views::FocusRing* ring = views::FocusRing::Get(sidebar()->row_at(0));
  ASSERT_TRUE(ring);
  // size-focus-ring-width and size-focus-ring-offset (2 px each). The halo
  // inset is the distance of the stroke's middle from the row: -(2 + 2 / 2).
  EXPECT_EQ(ring->GetHaloThickness(), 2.f);
  EXPECT_EQ(ring->GetHaloInset(), -3.f);
}

IN_PROC_BROWSER_TEST_F(AerieSidebarBrowserTest, TheF6PanesAreTheTopRowThePillTheTodayListAndThenThePage) {
  ASSERT_TRUE(WaitFor([&] { return sidebar()->row_count() == 1u; }));
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  std::vector<views::View*> panes;
  browser_view->GetAccessiblePanes(&panes);

  auto index_of = [&](views::View* pane) {
    return std::find(panes.begin(), panes.end(), pane) - panes.begin();
  };
  const ptrdiff_t none = static_cast<ptrdiff_t>(panes.size());
  ASSERT_NE(index_of(sidebar()->top_row()), none);
  ASSERT_NE(index_of(sidebar()->address_pill()), none);
  ASSERT_NE(index_of(sidebar()->today_tree()), none);
  EXPECT_LT(index_of(sidebar()->top_row()), index_of(sidebar()->address_pill()));
  EXPECT_LT(index_of(sidebar()->address_pill()),
            index_of(sidebar()->today_tree()));

  // Every pane of the sidebar comes before the page, and the sidebar is not
  // also in the list as one pane.
  EXPECT_EQ(index_of(region()), none);
  std::vector<views::View*> page_panes = browser_view->multi_contents_view()->GetAccessiblePanes();
  ASSERT_FALSE(page_panes.empty());
  EXPECT_GT(index_of(page_panes.front()), index_of(sidebar()->today_tree()));
  for (views::View* pane : panes) {
    if (sidebar()->Contains(pane)) {
      EXPECT_LT(index_of(pane), index_of(page_panes.front()));
    }
  }
}

IN_PROC_BROWSER_TEST_F(AerieSidebarBrowserTest, TheAccessibilityTreeHasTheRolesNamesAndStatesOfTheDesign) {
  chrome::ExecuteCommand(browser(), IDC_NEW_TAB);
  ASSERT_TRUE(WaitFor([&] { return sidebar()->row_count() == 2u; }));

  EXPECT_EQ(RoleOf(sidebar()), ax::mojom::Role::kNavigation);
  EXPECT_EQ(NameOf(sidebar()), "Sidebar");
  EXPECT_EQ(RoleOf(sidebar()->top_row()), ax::mojom::Role::kToolbar);
  EXPECT_FALSE(NameOf(sidebar()->top_row()).empty());
  EXPECT_FALSE(NameOf(sidebar()->address_pill()).empty());

  // The Today list: a tree named "Today" of treeitems at level 1, the active
  // tab selected.
  views::View* tree = sidebar()->today_tree();
  EXPECT_EQ(RoleOf(tree), ax::mojom::Role::kTree);
  EXPECT_EQ(NameOf(tree), "Today");
  int selected_rows = 0;
  for (size_t i = 0; i < sidebar()->row_count(); ++i) {
    AerieSidebarRowView* row = sidebar()->row_at(i);
    EXPECT_EQ(row->parent(), tree);
    ui::AXNodeData data;
    row->GetViewAccessibility().GetAccessibleNodeData(&data);
    EXPECT_EQ(data.role, ax::mojom::Role::kTreeItem);
    EXPECT_EQ(data.GetIntAttribute(ax::mojom::IntAttribute::kHierarchicalLevel),
              1);
    EXPECT_FALSE(data.GetStringAttribute(ax::mojom::StringAttribute::kName).empty());
    EXPECT_TRUE(data.HasBoolAttribute(ax::mojom::BoolAttribute::kSelected));
    if (data.GetBoolAttribute(ax::mojom::BoolAttribute::kSelected)) {
      ++selected_rows;
      EXPECT_TRUE(row->selected());
    }
  }
  EXPECT_EQ(selected_rows, 1);

  // The resizer is a separator named "Resize sidebar" (the splitter role is
  // what Chromium maps to a separator that can be focused).
  EXPECT_EQ(RoleOf(region()->resize_area_for_testing()),
            ax::mojom::Role::kSplitter);
  EXPECT_EQ(NameOf(region()->resize_area_for_testing()), "Resize sidebar");

  // The close button on a row has a name.
  auto* close = FindDescendant<views::ImageButton>(sidebar()->row_at(0));
  ASSERT_TRUE(close);
  EXPECT_EQ(NameOf(close), "Close tab");
}
