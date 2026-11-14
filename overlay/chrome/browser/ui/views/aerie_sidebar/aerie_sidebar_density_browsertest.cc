// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

// Browser tests of the sidebar's density, side and auto-hide (issue #184): the
// four densities and both sides, kept across a restart; the rail's icons-only
// column; the floating sidebar that leaves the page card where it is; the
// peek's busy sources; and the timers (one at most, no poll while hidden).

#include <string>

#include "base/run_loop.h"
#include "ui/aura/window.h"
#include "ui/aura/window_tree_host.h"
#include "ui/events/test/event_generator.h"
#include "base/task/single_thread_task_runner.h"
#include "base/test/run_until.h"
#include "base/time/time.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_peek_driver.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_regions.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_row_view.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_tile_view.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_top_row.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_view.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/multi_contents_view.h"
#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"
#include "chrome/common/pref_names.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "components/aerie_design_tokens/aerie_tokens.h"
#include "components/aerie_sidebar/aerie_sidebar_density.h"
#include "components/aerie_sidebar/aerie_sidebar_layout.h"
#include "components/aerie_sidebar/aerie_sidebar_peek.h"
#include "components/aerie_sidebar/aerie_sidebar_prefs.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/resize_area.h"
#include "ui/views/test/views_test_utils.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"

namespace {

using aerie_sidebar::PeekState;

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

void SpinFor(base::TimeDelta delay) {
  base::RunLoop loop;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, loop.QuitClosure(), delay);
  loop.Run();
}

}  // namespace

class AerieSidebarDensityBrowserTest : public InProcessBrowserTest {
 protected:
  // The headless test window is a few hundred px wide and a few tall, too small
  // for a column of rows; a real window size is what the layout is for.
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    browser_view()->GetWidget()->SetBounds(gfx::Rect(0, 0, 1100, 750));
    Settle();
  }

  BrowserView* browser_view() {
    return BrowserView::GetBrowserViewForBrowser(browser());
  }
  VerticalTabStripRegionView* region() {
    return browser_view()->vertical_tab_strip_region_view_for_testing();
  }
  AerieSidebarView* sidebar() {
    return FindDescendant<AerieSidebarView>(region());
  }
  PrefService* prefs() { return browser()->GetProfile()->GetPrefs(); }
  AerieSidebarPeekDriver* peek() { return sidebar()->peek_driver(); }

  // Sets the prefs and lets the layout rest.
  void Choose(const char* density, const char* side = "left") {
    prefs()->SetString(aerie_sidebar::kSidePref, side);
    prefs()->SetString(aerie_sidebar::kDensityPref, density);
    Settle();
  }
  void Settle() {
    base::RunLoop().RunUntilIdle();
    views::test::RunScheduledLayout(browser_view());
    SpinFor(base::Milliseconds(350));
    views::test::RunScheduledLayout(browser_view());
  }
  // The page card: where the web contents sit.
  gfx::Rect Page() { return browser_view()->multi_contents_view()->bounds(); }
  bool Reaches(PeekState state) {
    return base::test::RunUntil(
        [&] { return peek()->controller().state() == state; });
  }
  // The omnibox has focus in a new window, and it holds the sidebar open (a
  // busy source). The page takes the focus for the tests of the leave.
  void FocusPage() {
    browser_view()->GetFocusManager()->ClearFocus();
    browser()->tab_strip_model()->GetActiveWebContents()->Focus();
    sidebar()->SyncPeekBusy();
  }
  std::string FocusedName() {
    views::View* v = browser_view()->GetFocusManager()->GetFocusedView();
    return v ? std::string(v->GetClassName()) : "none";
  }
  // The pointer goes to the hot zone on the sidebar's edge and the sidebar
  // peeks (after the real 150 ms of delay).
  // A real pointer event, through the window, to a point in screen px.
  void MovePointerTo(const gfx::Point& screen_point) {
    views::Widget* widget = browser_view()->GetWidget();
    ui::test::EventGenerator generator(
        widget->GetNativeWindow()->GetRootWindow(), widget->GetNativeWindow());
    generator.MoveMouseRelativeTo(
        widget->GetNativeWindow(),
        screen_point - widget->GetWindowBoundsInScreen().OffsetFromOrigin());
  }
  void PeekOpen() {
    MovePointerTo(peek()->HotZoneInScreen().CenterPoint());
    ASSERT_TRUE(
        base::test::RunUntil([&] { return peek()->controller().visible(); }));
  }
};

IN_PROC_BROWSER_TEST_F(AerieSidebarDensityBrowserTest,
                       TheDefaultIsComfortableOnTheLeft) {
  EXPECT_EQ(prefs()->GetString(aerie_sidebar::kDensityPref), "comfortable");
  EXPECT_EQ(prefs()->GetString(aerie_sidebar::kSidePref), "left");
  EXPECT_FALSE(sidebar()->side_on_right());
  EXPECT_EQ(sidebar()->density(), aerie_sidebar::Density::kComfortable);
}

// One case per density: the row height is the generated token, and the column
// keeps the width the user chose, except the rail's 56 px.
IN_PROC_BROWSER_TEST_F(AerieSidebarDensityBrowserTest, EachDensityLaysOutOnce) {
  ASSERT_TRUE(
      base::test::RunUntil([&] { return sidebar()->row_count() == 1u; }));
  // The width the window gives the column (it is not always the pref: the
  // window may be too narrow for it).
  Choose("comfortable");
  const int chosen_width = region()->width();
  struct Case {
    const char* density;
    int row_height;
    int width;
  } cases[] = {
      {"comfortable", aerie::tokens::kSizeRowHeightComfortable, -1},
      {"compact", aerie::tokens::kSizeRowHeightCompact, -1},
      {"rail", aerie::tokens::kSizeRailTile, aerie::tokens::kSizeRailWidth},
  };
  for (const Case& c : cases) {
    SCOPED_TRACE(c.density);
    Choose(c.density);
    AerieSidebarRowView* row = sidebar()->row_at(0);
    ASSERT_TRUE(row);
    EXPECT_EQ(row->height(), c.row_height);
    EXPECT_EQ(region()->width(), c.width > 0 ? c.width : chosen_width);
  }
}

IN_PROC_BROWSER_TEST_F(AerieSidebarDensityBrowserTest,
                       TheSideMirrorsTheWholeWindow) {
  Choose("comfortable", "left");
  const int chosen_width = region()->width();
  EXPECT_LT(region()->x(), Page().x());
  EXPECT_FALSE(prefs()->GetBoolean(prefs::kHeliumVerticalRightAligned));
  Choose("comfortable", "right");
  EXPECT_TRUE(sidebar()->side_on_right());
  EXPECT_TRUE(prefs()->GetBoolean(prefs::kHeliumVerticalRightAligned));
  EXPECT_GT(region()->x(), Page().x());
  EXPECT_EQ(region()->bounds().right(), browser_view()->width());
  // The width the user chose is the same on both sides.
  EXPECT_EQ(region()->width(), chosen_width);
  Choose("comfortable", "left");
  EXPECT_FALSE(prefs()->GetBoolean(prefs::kHeliumVerticalRightAligned));
}

IN_PROC_BROWSER_TEST_F(AerieSidebarDensityBrowserTest,
                       PRE_DensityAndSideSurviveARestart) {
  Choose("rail", "right");
}

IN_PROC_BROWSER_TEST_F(AerieSidebarDensityBrowserTest,
                       DensityAndSideSurviveARestart) {
  EXPECT_EQ(prefs()->GetString(aerie_sidebar::kDensityPref), "rail");
  EXPECT_EQ(prefs()->GetString(aerie_sidebar::kSidePref), "right");
  Settle();
  EXPECT_EQ(sidebar()->density(), aerie_sidebar::Density::kRail);
  EXPECT_TRUE(sidebar()->side_on_right());
  EXPECT_EQ(region()->width(), aerie::tokens::kSizeRailWidth);
  EXPECT_EQ(region()->bounds().right(), browser_view()->width());
}

IN_PROC_BROWSER_TEST_F(AerieSidebarDensityBrowserTest, TheRailShowsOnlyIcons) {
  ASSERT_TRUE(
      base::test::RunUntil([&] { return sidebar()->row_count() == 1u; }));
  AerieSidebarRowView* row = sidebar()->row_at(0);
  const std::u16string title(row->title_view()->GetText());
  Choose("rail");
  EXPECT_FALSE(row->title_view()->GetVisible());
  EXPECT_FALSE(row->close_button()->GetVisible());
  // The tile is a square of 34 px, the favicon in its middle.
  EXPECT_EQ(row->height(), aerie::tokens::kSizeRailTile);
  EXPECT_EQ(row->width(), aerie::tokens::kSizeRailTile);
  // The section label and the resizer are hidden.
  EXPECT_FALSE(sidebar()->today_header_for_testing()->GetVisible());
  views::ResizeArea* resize = FindDescendant<views::ResizeArea>(region());
  ASSERT_TRUE(resize);
  EXPECT_FALSE(resize->GetVisible());
  // The address pill is a square Guard button.
  ASSERT_TRUE(sidebar()->guard_button());
  EXPECT_TRUE(sidebar()->guard_button()->GetVisible());
  EXPECT_EQ(sidebar()->guard_button()->width(), aerie::tokens::kSizeRailTile);
  EXPECT_EQ(sidebar()->guard_button()->height(), aerie::tokens::kSizeRailTile);
  // Back to comfortable: everything is back.
  Choose("comfortable");
  EXPECT_TRUE(row->title_view()->GetVisible());
  EXPECT_TRUE(sidebar()->today_header_for_testing()->GetVisible());
  EXPECT_FALSE(sidebar()->guard_button()->GetVisible());
  EXPECT_EQ(row->title_view()->GetText(), title);
}

// The full titles stay as the accessible names (issue #184).
IN_PROC_BROWSER_TEST_F(AerieSidebarDensityBrowserTest,
                       TheRailKeepsTheFullNamesInTheAccessibilityTree) {
  ASSERT_TRUE(
      base::test::RunUntil([&] { return sidebar()->row_count() == 1u; }));
  AerieSidebarRowView* row = sidebar()->row_at(0);
  Choose("comfortable");
  const std::string name = NameOf(row);
  ASSERT_FALSE(name.empty());
  Choose("rail");
  EXPECT_EQ(NameOf(row), name);
  EXPECT_FALSE(NameOf(sidebar()->guard_button()).empty());
  EXPECT_FALSE(NameOf(sidebar()->new_tab_row_for_testing()).empty());
}

// Auto-hide: the sidebar leaves the layout; the page card does not move when it
// peeks; and Ctrl+S docks it.
IN_PROC_BROWSER_TEST_F(AerieSidebarDensityBrowserTest,
                       AutoHideTakesNoRoomAndThePeekDoesNotMoveThePage) {
  Choose("comfortable");
  const gfx::Rect docked = Page();
  Choose("auto-hide");
  ASSERT_TRUE(peek()->active());
  EXPECT_FALSE(sidebar()->GetVisible());
  EXPECT_FALSE(prefs()->GetBoolean(aerie_sidebar::kVisiblePref));
  const gfx::Rect hidden = Page();
  EXPECT_GT(hidden.width(), docked.width());
  PeekOpen();
  Settle();
  EXPECT_TRUE(sidebar()->GetVisible());
  // The page card did not move; the sidebar floats over it.
  EXPECT_EQ(Page(), hidden);
  EXPECT_EQ(region()->width(), sidebar()->column_width());
  // Ctrl+S docks it: the page gives the room up.
  chrome::ExecuteCommand(browser(), IDC_CTRL_S_SHORTCUT);
  Settle();
  EXPECT_TRUE(prefs()->GetBoolean(aerie_sidebar::kVisiblePref));
  EXPECT_FALSE(peek()->active());
  EXPECT_EQ(Page().width(), docked.width());
  // And again: undocked, hidden.
  chrome::ExecuteCommand(browser(), IDC_CTRL_S_SHORTCUT);
  Settle();
  EXPECT_FALSE(prefs()->GetBoolean(aerie_sidebar::kVisiblePref));
  EXPECT_TRUE(peek()->active());
}

IN_PROC_BROWSER_TEST_F(AerieSidebarDensityBrowserTest,
                       CtrlSTogglesInEveryDensity) {
  for (const char* density : {"comfortable", "compact", "rail"}) {
    SCOPED_TRACE(density);
    Choose(density);
    ASSERT_TRUE(prefs()->GetBoolean(aerie_sidebar::kVisiblePref));
    chrome::ExecuteCommand(browser(), IDC_CTRL_S_SHORTCUT);
    EXPECT_FALSE(prefs()->GetBoolean(aerie_sidebar::kVisiblePref));
    chrome::ExecuteCommand(browser(), IDC_CTRL_S_SHORTCUT);
    EXPECT_TRUE(prefs()->GetBoolean(aerie_sidebar::kVisiblePref));
  }
}

// An idle browser has no repeating timer from this slice, and the sidebar holds
// at most one pending timer (issue #184).
IN_PROC_BROWSER_TEST_F(AerieSidebarDensityBrowserTest, TimersAreOffWhileHidden) {
  // Not auto-hide: the driver is off and has nothing running.
  EXPECT_FALSE(peek()->active());
  EXPECT_FALSE(peek()->monitor_for_testing());
  EXPECT_FALSE(peek()->poll_running_for_testing());
  EXPECT_FALSE(peek()->controller().timer_pending());
  Choose("auto-hide");
  FocusPage();
  ASSERT_TRUE(peek()->active());
  EXPECT_EQ(peek()->controller().state(), PeekState::kHidden);
  EXPECT_FALSE(peek()->poll_running_for_testing());
  EXPECT_FALSE(peek()->controller().timer_pending());
  // The pointer in the hot zone arms one timer, and no poll: it is inside the
  // window.
  MovePointerTo(peek()->HotZoneInScreen().CenterPoint());
  EXPECT_TRUE(peek()->controller().timer_pending());
  EXPECT_FALSE(peek()->poll_running_for_testing());
  ASSERT_TRUE(
      base::test::RunUntil([&] { return peek()->controller().visible(); }));
  // It leaves: back to hidden, nothing pending, no poll.
  MovePointerTo(browser_view()->GetBoundsInScreen().CenterPoint());
  ASSERT_TRUE(Reaches(PeekState::kHidden)) << FocusedName();
  EXPECT_FALSE(peek()->controller().timer_pending());
  EXPECT_FALSE(peek()->poll_running_for_testing());
}

// A rename holds the sidebar open until it ends.
IN_PROC_BROWSER_TEST_F(AerieSidebarDensityBrowserTest,
                       ARenameHoldsThePeekOpen) {
  ASSERT_TRUE(
      base::test::RunUntil([&] { return sidebar()->row_count() == 1u; }));
  Choose("auto-hide");
  FocusPage();
  PeekOpen();
  AerieSidebarRowView* row = sidebar()->row_at(0);
  row->BeginRename(u"Name", prefs());
  sidebar()->SyncPeekBusy();
  EXPECT_TRUE(peek()->controller().busy());
  // The pointer leaves; the sidebar stays.
  MovePointerTo(browser_view()->GetBoundsInScreen().CenterPoint());
  SpinFor(base::Milliseconds(500));
  EXPECT_TRUE(peek()->controller().visible());
  row->CancelRename();
  sidebar()->SyncPeekBusy();
  EXPECT_FALSE(peek()->controller().busy());
  EXPECT_TRUE(Reaches(PeekState::kHidden)) << FocusedName();
}
