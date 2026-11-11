// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

// Browser tests of the Aerie sidebar (issue #177): the Today list follows the
// tabs, Ctrl+S shows and hides it with the pref as its memory, a resize is
// clamped and stored, reduced motion never animates the transform, and the
// accessibility tree has the roles the design asks for.

#include <algorithm>
#include <optional>
#include <string>

#include "base/functional/bind.h"
#include "base/run_loop.h"
#include "base/strings/string_number_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "base/time/time.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/extensions/api/settings_private/prefs_util.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/view_ids.h"
#include "chrome/browser/aerie_tab_model/tab_model_service.h"
#include "chrome/browser/aerie_tab_model/tab_strip_adapter.h"
#include "chrome/browser/aerie_tab_model/tab_model_service_factory.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_focus_ring.h"
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
#include "components/aerie_design_tokens/aerie_tokens.h"
#include "components/aerie_frame/aerie_frame_palette.h"
#include "components/aerie_tab_model/tab_model.h"
#include "components/aerie_adblock/browser/page_block_counter.h"
#include "components/aerie_sidebar/aerie_sidebar_layout.h"
#include "components/aerie_sidebar/aerie_sidebar_prefs.h"
#include "components/prefs/pref_change_registrar.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/dns/mock_host_resolver.h"
#include "url/gurl.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/color/color_provider_key.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/canvas.h"
#include "components/tabs/public/tab_interface.h"
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

// Item 6 of issue #215 (ADR 0008): Helium's layout and Zen mode are locked in
// the pref service, not held by writing them back.
IN_PROC_BROWSER_TEST_F(AerieSidebarBrowserTest, TheHeliumLayoutPrefsAreLocked) {
  EXPECT_EQ(prefs()->GetInteger(prefs::kHeliumLayout),
            aerie_sidebar::kHeliumLayoutVertical);
  EXPECT_FALSE(prefs()->GetBoolean(prefs::kHeliumZenMode));
  EXPECT_FALSE(prefs()->IsUserModifiablePreference(prefs::kHeliumLayout));
  EXPECT_FALSE(prefs()->IsUserModifiablePreference(prefs::kHeliumZenMode));

  // A writer loses at once. An observer may be told that the user layer
  // changed, but what it reads is the locked value, so nothing has to write it
  // back (the old hold did).
  int wrong_reads = 0;
  PrefChangeRegistrar registrar;
  registrar.Init(prefs());
  registrar.Add(prefs::kHeliumLayout,
                base::BindRepeating(
                    [](PrefService* p, int* n) {
                      *n += p->GetInteger(prefs::kHeliumLayout) !=
                            aerie_sidebar::kHeliumLayoutVertical;
                    },
                    base::Unretained(prefs()), base::Unretained(&wrong_reads)));
  registrar.Add(prefs::kHeliumZenMode,
                base::BindRepeating(
                    [](PrefService* p, int* n) {
                      *n += p->GetBoolean(prefs::kHeliumZenMode) ? 1 : 0;
                    },
                    base::Unretained(prefs()), base::Unretained(&wrong_reads)));
  prefs()->SetInteger(prefs::kHeliumLayout, 3);  // Dynamic.
  prefs()->SetBoolean(prefs::kHeliumZenMode, true);
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(prefs()->GetInteger(prefs::kHeliumLayout),
            aerie_sidebar::kHeliumLayoutVertical);
  EXPECT_FALSE(prefs()->GetBoolean(prefs::kHeliumZenMode));
  EXPECT_EQ(wrong_reads, 0);

  // The sidebar is still the layout.
  EXPECT_TRUE(sidebar());
}

IN_PROC_BROWSER_TEST_F(AerieSidebarBrowserTest, SettingsCannotReadOrWriteTheLockedPrefs) {
  extensions::PrefsUtil util(browser()->GetProfile());
  EXPECT_FALSE(util.GetPref(prefs::kHeliumLayout));
  EXPECT_FALSE(util.GetPref(prefs::kHeliumZenMode));
  EXPECT_FALSE(util.GetPref(prefs::kHeliumZenModeSidebarPinned));
  EXPECT_FALSE(util.GetPref(prefs::kHeliumZenModeTopChromePinned));

  base::Value dynamic(3);
  EXPECT_EQ(util.SetPref(prefs::kHeliumLayout, &dynamic),
            extensions::settings_private::SetPrefResult::PREF_NOT_FOUND);
  base::Value on(true);
  EXPECT_EQ(util.SetPref(prefs::kHeliumZenMode, &on),
            extensions::settings_private::SetPrefResult::PREF_NOT_FOUND);
  EXPECT_EQ(prefs()->GetInteger(prefs::kHeliumLayout),
            aerie_sidebar::kHeliumLayoutVertical);
}

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
  AerieSidebarFocusRing* ring = AerieSidebarFocusRing::Get(sidebar()->row_at(0));
  ASSERT_TRUE(ring);
  // size-focus-ring-width and size-focus-ring-offset (2 px each).
  EXPECT_EQ(ring->thickness(), aerie::tokens::kSizeFocusRingWidth);
  EXPECT_EQ(ring->thickness(), 2.f);
  EXPECT_EQ(ring->offset(), aerie::tokens::kSizeFocusRingOffset);
  EXPECT_EQ(ring->offset(), 2.f);
  // It sits outside the row by offset plus width on every side, and shows only
  // while the row has focus.
  EXPECT_FALSE(ring->shown());
  sidebar()->row_at(0)->RequestFocus();
  EXPECT_TRUE(sidebar()->row_at(0)->HasFocus());
  EXPECT_TRUE(ring->shown());
  gfx::Rect expected = sidebar()->row_at(0)->GetLocalBounds();
  expected.Outset(4);
  EXPECT_EQ(ring->bounds(), expected);
}

namespace {
// True when nothing between `view` and the nearest layer above it clips what
// `view` paints. A Views view without a layer paints inside the clip of every
// ancestor without a layer, and each such view clips to its own bounds, so a
// child that draws outside its parent (a focus ring) is cut off unless it has a
// layer of its own.
bool PaintsOutsideItsParentUnclipped(views::View* view) {
  if (view->layer()) {
    return true;
  }
  gfx::Rect rect = view->bounds();
  for (views::View* v = view->parent(); v; v = v->parent()) {
    if (!gfx::Rect(v->size()).Contains(rect)) {
      return false;
    }
    if (v->layer()) {
      return true;
    }
    rect.Offset(v->bounds().OffsetFromOrigin());
  }
  return true;
}

// What the ring's own paint puts on a transparent canvas of its size.
SkBitmap PaintRing(AerieSidebarFocusRing* ring) {
  gfx::Canvas canvas(ring->size(), 1.f, /*is_opaque=*/false);
  canvas.DrawColor(SK_ColorTRANSPARENT, SkBlendMode::kSrc);
  ring->OnPaint(&canvas);
  return canvas.GetBitmap();
}
}  // namespace

// The ring of a Today row drew only as four dots at its corners: the row has no
// layer, so it clips its children to its own bounds, and the ring sits outside
// them. The check reads the ring's own paint and whether an ancestor cuts it.
IN_PROC_BROWSER_TEST_F(AerieSidebarBrowserTest, TheFocusRingOfARowIsPaintedAndNotClipped) {
  ASSERT_TRUE(WaitFor([&] { return sidebar()->row_count() == 1u; }));
  AerieSidebarRowView* row = sidebar()->row_at(0);
  AerieSidebarFocusRing* ring = AerieSidebarFocusRing::Get(row);
  row->RequestFocus();
  ASSERT_TRUE(ring->shown());
  EXPECT_TRUE(PaintsOutsideItsParentUnclipped(ring))
      << "an ancestor without a layer clips the ring to the row";

  const SkBitmap bitmap = PaintRing(ring);
  ASSERT_EQ(bitmap.width(), ring->width());
  const int w = bitmap.width();
  const int h = bitmap.height();
  const SkColor colour = ring->color();
  // The stroke is the outer 2 px of the ring, on the middle of every side.
  for (const gfx::Point& p : {gfx::Point(w / 2, 1), gfx::Point(w / 2, h - 2),
                              gfx::Point(1, h / 2), gfx::Point(w - 2, h / 2)}) {
    EXPECT_EQ(bitmap.getColor(p.x(), p.y()), colour) << p.ToString();
  }
  // The gap between the stroke and the row stays clear.
  EXPECT_EQ(SkColorGetA(bitmap.getColor(w / 2, h / 2)), 0u);
  EXPECT_EQ(SkColorGetA(bitmap.getColor(w / 2, 3)), 0u);
}

// --- Theme and Workspace colours (issue #215, item 7) ----------------------

class AerieSidebarPaletteBrowserTest : public AerieSidebarBrowserTest {
 protected:
  void SetMode(aerie::tokens::ColorMode mode) {
    BrowserView::GetBrowserViewForBrowser(browser())
        ->GetWidget()
        ->SetColorModeOverride(mode == aerie::tokens::ColorMode::kDark
                                   ? ui::ColorProviderKey::ColorMode::kDark
                                   : ui::ColorProviderKey::ColorMode::kLight);
  }
  void SetColour(const std::string& colour) {
    aerie_tab_model::TabModelServiceFactory::GetForProfile(
        browser()->GetProfile())
        ->model()
        ->UpdateWorkspace(
            [&](aerie_tab_model::Workspace& w) { w.colour = colour; });
  }
  SkColor BackgroundOf(AerieSidebarRowView* row) {
    return row->content()->background()->color().ResolveToSkColor(
        row->GetColorProvider());
  }
};

IN_PROC_BROWSER_TEST_F(AerieSidebarPaletteBrowserTest, RowsDrawTheThemeTokensInBothModes) {
  ASSERT_TRUE(WaitFor([&] { return sidebar()->row_count() == 1u; }));
  AerieSidebarRowView* row = sidebar()->row_at(0);
  for (aerie::tokens::ColorMode mode : {aerie::tokens::ColorMode::kLight,
                                        aerie::tokens::ColorMode::kDark}) {
    SetMode(mode);
    const auto& tokens = aerie::tokens::GetThemeTokens(
        aerie_frame::kDefaultTheme, mode);
    ASSERT_TRUE(row->selected());
    // A change of the colour mode reaches the views with the theme change.
    ASSERT_TRUE(WaitFor([&] {
      return BackgroundOf(row) == tokens.color_row_selected;
    }));
    EXPECT_EQ(sidebar()->palette().row_selected, tokens.color_row_selected);
    EXPECT_EQ(sidebar()->palette().row_hover, tokens.color_row_hover);
    EXPECT_EQ(row->palette().text, sidebar()->palette().text);
    EXPECT_EQ(sidebar()->top_row()->palette().text, sidebar()->palette().text);
  }
  // The two modes really differ.
  SetMode(aerie::tokens::ColorMode::kLight);
  ASSERT_TRUE(WaitFor([&] {
    return sidebar()->palette().text ==
           aerie_frame::ResolveFramePalette(aerie_frame::kDefaultTheme,
                                            aerie::tokens::ColorMode::kLight,
                                            "dusk")
               .text;
  }));
  const SkColor light = sidebar()->palette().text;
  SetMode(aerie::tokens::ColorMode::kDark);
  ASSERT_TRUE(WaitFor([&] { return sidebar()->palette().text != light; }));
}

IN_PROC_BROWSER_TEST_F(AerieSidebarPaletteBrowserTest, TheFocusRingFollowsTheWorkspaceColour) {
  ASSERT_TRUE(WaitFor([&] { return sidebar()->row_count() == 1u; }));
  AerieSidebarRowView* row = sidebar()->row_at(0);
  SetColour("dusk");
  const SkColor dusk = AerieSidebarFocusRing::Get(row)->color();
  EXPECT_EQ(dusk, sidebar()->palette().accent);
  SetColour("tide");
  const SkColor tide = AerieSidebarFocusRing::Get(row)->color();
  EXPECT_NE(tide, dusk);
  EXPECT_EQ(tide, aerie_frame::ResolveFramePalette(
                      aerie_frame::kDefaultTheme, aerie::tokens::ColorMode::kLight,
                      "tide").accent);
  // The window controls and the toggle of the top row follow it too.
  EXPECT_EQ(AerieSidebarFocusRing::Get(sidebar()->top_row()->toggle_button())
                ->color(),
            tide);
}

IN_PROC_BROWSER_TEST_F(AerieSidebarPaletteBrowserTest, ANewRowGetsThePaletteAtOnce) {
  ASSERT_TRUE(WaitFor([&] { return sidebar()->row_count() == 1u; }));
  SetMode(aerie::tokens::ColorMode::kDark);
  SetColour("moss");
  ASSERT_TRUE(WaitFor([&] {
    return sidebar()->palette().row_hover ==
           aerie::tokens::GetThemeTokens(aerie_frame::kDefaultTheme,
                                         aerie::tokens::ColorMode::kDark)
               .color_row_hover;
  }));
  chrome::ExecuteCommand(browser(), IDC_NEW_TAB);
  ASSERT_TRUE(WaitFor([&] { return sidebar()->row_count() == 2u; }));
  EXPECT_EQ(sidebar()->row_at(1)->palette().accent, sidebar()->palette().accent);
  EXPECT_EQ(sidebar()->row_at(1)->palette().row_hover,
            aerie::tokens::GetThemeTokens(aerie_frame::kDefaultTheme,
                                          aerie::tokens::ColorMode::kDark)
                .color_row_hover);
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


// --- Tab drag and drop (issue #215, item 8) ----------------------------------

class AerieSidebarDragBrowserTest : public AerieSidebarBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    AerieSidebarBrowserTest::SetUpOnMainThread();
    // The test window can be tiny; a drop is "outside" past its bounds.
    BrowserView::GetBrowserViewForBrowser(browser())->GetWidget()->SetBounds(
        gfx::Rect(0, 0, 1200, 900));
    // Motion is not what these tests are about; the uitest has it.
    reduced_motion_.emplace(
        gfx::AnimationTestApi::SetPrefersReducedMotionForTesting(true));
  }

  aerie_tab_model::TabModelService* service() {
    return aerie_tab_model::TabModelServiceFactory::GetForProfile(
        browser()->GetProfile());
  }

  void OpenTabs(size_t total) {
    while (browser()->tab_strip_model()->count() < static_cast<int>(total)) {
      chrome::ExecuteCommand(browser(), IDC_NEW_TAB);
    }
    ASSERT_TRUE(WaitFor([&] { return sidebar()->row_count() == total; }));
    ASSERT_TRUE(WaitFor([&] {
      for (size_t i = 0; i < total; ++i) {
        if (sidebar()->row_at(i)->height_animating() ||
            sidebar()->row_at(i)->GetPreferredSize().height() != 34) {
          return false;
        }
      }
      return sidebar()->closing_row_count() == 0u;
    }));
  }

  // The root location of the middle of `row`, `dy` px lower.
  gfx::Point RootPoint(AerieSidebarRowView* row, int dy) {
    gfx::Point p = row->GetLocalBounds().CenterPoint();
    views::View::ConvertPointToWidget(row, &p);
    p.Offset(0, dy);
    return p;
  }

  ui::MouseEvent Mouse(ui::EventType type, AerieSidebarRowView* row,
                       const gfx::Point& root) {
    gfx::Point local = root;
    views::View::ConvertPointFromWidget(row, &local);
    return ui::MouseEvent(type, local, root, base::TimeTicks::Now(),
                          ui::EF_LEFT_MOUSE_BUTTON, ui::EF_LEFT_MOUSE_BUTTON);
  }

  void Press(AerieSidebarRowView* row) {
    ui::MouseEvent event =
        Mouse(ui::EventType::kMousePressed, row, RootPoint(row, 0));
    row->OnMousePressed(event);
  }
  void DragTo(AerieSidebarRowView* row, int dy, int steps = 5) {
    const gfx::Point start = RootPoint(row, 0);
    start_ = start;
    for (int i = 1; i <= steps; ++i) {
      gfx::Point p = start;
      p.Offset(0, dy * i / steps);
      ui::MouseEvent event = Mouse(ui::EventType::kMouseDragged, row, p);
      row->OnMouseDragged(event);
    }
  }
  void Release(AerieSidebarRowView* row, int dy, int x_offset = 0) {
    gfx::Point p = start_;
    p.Offset(x_offset, dy);
    ui::MouseEvent event = Mouse(ui::EventType::kMouseReleased, row, p);
    row->OnMouseReleased(event);
  }

  int Pitch() {
    return sidebar()->row_at(1)->y() - sidebar()->row_at(0)->y();
  }

  // The record id of the tab at `index` of the strip.
  std::string IdAt(int index) {
    return service()->adapter()->RecordIdForTab(
        browser()->tab_strip_model()->GetTabAtIndex(index));
  }

 private:
  gfx::Point start_;
  std::optional<gfx::AnimationTestApi::PrefersReducedMotionResetter>
      reduced_motion_;
};

IN_PROC_BROWSER_TEST_F(AerieSidebarDragBrowserTest, DraggingARowDownReordersTheTabsAndTheList) {
  OpenTabs(3);
  const std::string first = IdAt(0);
  const std::string second = IdAt(1);
  const std::string third = IdAt(2);
  ASSERT_EQ(sidebar()->tab_id_at(0), first);
  tabs::TabInterface* active = browser()->tab_strip_model()->GetActiveTab();

  AerieSidebarRowView* row = sidebar()->row_at(0);
  Press(row);
  DragTo(row, 2 * Pitch());
  EXPECT_TRUE(sidebar()->dragging());
  EXPECT_EQ(sidebar()->drag_target_index(), 2);
  EXPECT_TRUE(row->dragging());
  Release(row, 2 * Pitch());

  // The strip, the Tab Model and the list all have the new order, and the open
  // tab is still the open tab.
  EXPECT_FALSE(sidebar()->dragging());
  EXPECT_EQ(IdAt(0), second);
  EXPECT_EQ(IdAt(1), third);
  EXPECT_EQ(IdAt(2), first);
  EXPECT_EQ(sidebar()->tab_id_at(0), second);
  EXPECT_EQ(sidebar()->tab_id_at(1), third);
  EXPECT_EQ(sidebar()->tab_id_at(2), first);
  EXPECT_EQ(browser()->tab_strip_model()->GetActiveTab(), active);
  for (int i = 0; i < 3; ++i) {
    EXPECT_EQ(service()->model()->GetTab(IdAt(i))->position, i);
  }
  // The views are in list order too.
  EXPECT_LT(sidebar()->row_at(0)->y(), sidebar()->row_at(1)->y());
  EXPECT_LT(sidebar()->row_at(1)->y(), sidebar()->row_at(2)->y());
}

IN_PROC_BROWSER_TEST_F(AerieSidebarDragBrowserTest, DraggingARowUpMovesItAboveTheOthers) {
  OpenTabs(3);
  const std::string first = IdAt(0);
  const std::string third = IdAt(2);
  AerieSidebarRowView* row = sidebar()->row_at(2);
  Press(row);
  DragTo(row, -2 * Pitch());
  EXPECT_EQ(sidebar()->drag_target_index(), 0);
  Release(row, -2 * Pitch());
  EXPECT_EQ(IdAt(0), third);
  EXPECT_EQ(IdAt(1), first);
  EXPECT_EQ(sidebar()->tab_id_at(0), third);
}

IN_PROC_BROWSER_TEST_F(AerieSidebarDragBrowserTest, ADropOnItsOwnSlotChangesNothing) {
  OpenTabs(3);
  const std::string first = IdAt(0);
  const std::string second = IdAt(1);
  AerieSidebarRowView* row = sidebar()->row_at(0);
  Press(row);
  DragTo(row, Pitch() / 4);
  Release(row, Pitch() / 4);
  EXPECT_EQ(IdAt(0), first);
  EXPECT_EQ(IdAt(1), second);
  EXPECT_FALSE(sidebar()->dragging());
}

IN_PROC_BROWSER_TEST_F(AerieSidebarDragBrowserTest, TheRowStaysBetweenTheFirstAndLastSlot) {
  OpenTabs(3);
  AerieSidebarRowView* row = sidebar()->row_at(0);
  Press(row);
  DragTo(row, 10 * Pitch());
  EXPECT_EQ(sidebar()->drag_target_index(), 2);
  EXPECT_EQ(row->drag_offset(), 2 * Pitch());
  Release(row, 10 * Pitch());
}

IN_PROC_BROWSER_TEST_F(AerieSidebarDragBrowserTest, APressThatDoesNotMoveFarStillSelectsTheTab) {
  OpenTabs(3);
  ASSERT_EQ(browser()->tab_strip_model()->active_index(), 2);
  const std::string first = IdAt(0);
  AerieSidebarRowView* row = sidebar()->row_at(0);
  Press(row);
  DragTo(row, 2);
  EXPECT_FALSE(sidebar()->dragging());
  Release(row, 2);
  EXPECT_EQ(browser()->tab_strip_model()->active_index(), 0);
  EXPECT_EQ(IdAt(0), first);
}

IN_PROC_BROWSER_TEST_F(AerieSidebarDragBrowserTest, ADragDoesNotSelectTheTab) {
  OpenTabs(3);
  tabs::TabInterface* active = browser()->tab_strip_model()->GetActiveTab();
  AerieSidebarRowView* row = sidebar()->row_at(0);
  Press(row);
  DragTo(row, Pitch());
  Release(row, Pitch());
  // The tab that was open is still the open one (it moved up one place).
  EXPECT_EQ(browser()->tab_strip_model()->GetActiveTab(), active);
}

IN_PROC_BROWSER_TEST_F(AerieSidebarDragBrowserTest, ALoneRowCannotBeDragged) {
  ASSERT_EQ(sidebar()->row_count(), 1u);
  AerieSidebarRowView* row = sidebar()->row_at(0);
  Press(row);
  DragTo(row, 80);
  EXPECT_FALSE(sidebar()->dragging());
  EXPECT_FALSE(row->dragging());
  Release(row, 80);
}

IN_PROC_BROWSER_TEST_F(AerieSidebarDragBrowserTest, LosingTheMouseCaptureCancelsTheDrag) {
  OpenTabs(3);
  const std::string first = IdAt(0);
  AerieSidebarRowView* row = sidebar()->row_at(0);
  Press(row);
  DragTo(row, 2 * Pitch());
  ASSERT_TRUE(sidebar()->dragging());
  row->OnMouseCaptureLost();
  EXPECT_FALSE(sidebar()->dragging());
  EXPECT_EQ(IdAt(0), first);
  EXPECT_FALSE(row->has_drag_layers());
}

IN_PROC_BROWSER_TEST_F(AerieSidebarDragBrowserTest, ADropOutsideTheWindowMovesTheTabToANewWindow) {
  OpenTabs(3);
  const std::string first = IdAt(0);
  ASSERT_EQ(GlobalBrowserCollection::GetInstance()->GetSize(), 1u);
  AerieSidebarRowView* row = sidebar()->row_at(0);
  Press(row);
  DragTo(row, Pitch());
  // Far left of the window.
  Release(row, Pitch(), /*x_offset=*/-3000);
  EXPECT_TRUE(WaitFor([&] { return GlobalBrowserCollection::GetInstance()->GetSize() == 2u; }));
  EXPECT_EQ(browser()->tab_strip_model()->count(), 2);
  EXPECT_NE(IdAt(0), first);
  EXPECT_TRUE(WaitFor([&] { return sidebar()->row_count() == 2u; }));
  // The tab keeps its Tab Model record in the other window.
  EXPECT_TRUE(service()->model()->GetTab(first));
}

IN_PROC_BROWSER_TEST_F(AerieSidebarDragBrowserTest, TheListFollowsAMoveOfTheStripFromAnywhere) {
  OpenTabs(3);
  const std::string first = IdAt(0);
  browser()->tab_strip_model()->MoveWebContentsAt(0, 2, false);
  ASSERT_TRUE(WaitFor([&] { return sidebar()->tab_id_at(2) == first; }));
  for (int i = 0; i < 3; ++i) {
    EXPECT_EQ(service()->model()->GetTab(IdAt(i))->position, i);
  }
}
