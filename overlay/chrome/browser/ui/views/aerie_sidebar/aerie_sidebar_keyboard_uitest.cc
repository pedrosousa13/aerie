// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

// The keyboard in the sidebar (issue #215, item 3), with real key events: the
// roving tab stop of the Today list moves with the arrow keys, Home and End;
// Delete and Ctrl+W close the focused tab; and F6 moves focus top row, address
// pill, Today list, page. Run with
// ./aerie test-cpp --target aerie_interactive_ui_tests.

#include <optional>
#include <vector>

#include "base/test/run_until.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_regions.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_row_view.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_top_row.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_view.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/interactive_test_utils.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/animation/animation_test_api.h"
#include "ui/views/focus/focus_manager.h"
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

}  // namespace

class AerieSidebarKeyboardUiTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    // Rows that animate are still rows; the keys are what is tested here.
    reduced_motion_.emplace(
        gfx::AnimationTestApi::SetPrefersReducedMotionForTesting(true));
    ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));
  }

  BrowserView* browser_view() {
    return BrowserView::GetBrowserViewForBrowser(browser());
  }
  AerieSidebarView* sidebar() {
    return FindDescendant<AerieSidebarView>(
        browser_view()->vertical_tab_strip_region_view_for_testing());
  }
  views::View* focused() {
    return browser_view()->GetWidget()->GetFocusManager()->GetFocusedView();
  }

  // Three tabs, and three rows.
  void OpenThreeTabs() {
    chrome::ExecuteCommand(browser(), IDC_NEW_TAB);
    chrome::ExecuteCommand(browser(), IDC_NEW_TAB);
    ASSERT_TRUE(base::test::RunUntil([&] { return sidebar()->row_count() == 3u; }));
  }

  void Press(ui::KeyboardCode key, bool control = false, bool shift = false) {
    ASSERT_TRUE(ui_test_utils::SendKeyPressSync(browser(), key, control, shift,
                                                /*alt=*/false,
                                                /*command=*/false));
  }

  // Where focus is, as one of the regions the design names.
  enum class Place { kTopRow, kPill, kToday, kBottomBar, kElsewhere };
  Place PlaceOfFocus() {
    views::View* view = focused();
    if (!view) {
      return Place::kElsewhere;
    }
    if (sidebar()->top_row()->Contains(view)) {
      return Place::kTopRow;
    }
    if (sidebar()->address_pill()->Contains(view)) {
      return Place::kPill;
    }
    if (sidebar()->today_tree()->Contains(view)) {
      return Place::kToday;
    }
    if (sidebar()->bottom_bar()->Contains(view)) {
      return Place::kBottomBar;
    }
    return Place::kElsewhere;
  }

 private:
  std::optional<gfx::AnimationTestApi::PrefersReducedMotionResetter>
      reduced_motion_;
};

IN_PROC_BROWSER_TEST_F(AerieSidebarKeyboardUiTest, ArrowKeysHomeAndEndMoveTheTabStop) {
  ASSERT_TRUE(base::test::RunUntil([&] { return sidebar()->row_count() == 1u; }));
  OpenThreeTabs();

  // Focus the list: it goes to the row that holds the stop, the open tab (the
  // last one, because a new tab opens last and is selected).
  sidebar()->today_tree()->RequestFocus();
  ASSERT_EQ(focused(), sidebar()->row_at(2));

  Press(ui::VKEY_UP);
  EXPECT_EQ(focused(), sidebar()->row_at(1));
  EXPECT_EQ(sidebar()->tab_stop_row(), sidebar()->row_at(1));
  Press(ui::VKEY_HOME);
  EXPECT_EQ(focused(), sidebar()->row_at(0));
  Press(ui::VKEY_UP);  // A tree does not wrap.
  EXPECT_EQ(focused(), sidebar()->row_at(0));
  Press(ui::VKEY_DOWN);
  EXPECT_EQ(focused(), sidebar()->row_at(1));
  Press(ui::VKEY_END);
  EXPECT_EQ(focused(), sidebar()->row_at(2));
  Press(ui::VKEY_DOWN);
  EXPECT_EQ(focused(), sidebar()->row_at(2));

  // Exactly one row is a Tab stop at every moment, and it is the focused one.
  int stops = 0;
  for (size_t i = 0; i < sidebar()->row_count(); ++i) {
    stops += sidebar()->row_at(i)->is_tab_stop() ? 1 : 0;
  }
  EXPECT_EQ(stops, 1);
  EXPECT_TRUE(sidebar()->row_at(2)->is_tab_stop());
}

IN_PROC_BROWSER_TEST_F(AerieSidebarKeyboardUiTest, DeleteClosesTheFocusedTabAndFocusLandsOnTheNextRow) {
  OpenThreeTabs();
  sidebar()->today_tree()->RequestFocus();
  Press(ui::VKEY_HOME);
  Press(ui::VKEY_DOWN);
  ASSERT_EQ(focused(), sidebar()->row_at(1));
  content::WebContents* gone = browser()->tab_strip_model()->GetWebContentsAt(1);
  content::WebContents* next = browser()->tab_strip_model()->GetWebContentsAt(2);

  Press(ui::VKEY_DELETE);
  ASSERT_TRUE(base::test::RunUntil([&] { return sidebar()->row_count() == 2u; }));
  EXPECT_EQ(browser()->tab_strip_model()->count(), 2);
  EXPECT_EQ(browser()->tab_strip_model()->GetIndexOfWebContents(gone),
            TabStripModel::kNoTab);
  EXPECT_EQ(browser()->tab_strip_model()->GetIndexOfWebContents(next), 1);
  // The stop stays at the same index: it lands on what was the next row.
  ASSERT_TRUE(base::test::RunUntil([&] { return focused() == sidebar()->row_at(1); }));

  // Closing the last row puts the stop on the new last row.
  Press(ui::VKEY_END);
  Press(ui::VKEY_DELETE);
  ASSERT_TRUE(base::test::RunUntil([&] { return sidebar()->row_count() == 1u; }));
  ASSERT_TRUE(base::test::RunUntil([&] { return focused() == sidebar()->row_at(0); }));
}

IN_PROC_BROWSER_TEST_F(AerieSidebarKeyboardUiTest, CtrlWClosesTheFocusedTabNotTheSelectedOne) {
  OpenThreeTabs();
  TabStripModel* tabs = browser()->tab_strip_model();
  ASSERT_EQ(tabs->active_index(), 2);
  content::WebContents* first = tabs->GetWebContentsAt(0);
  content::WebContents* active = tabs->GetActiveWebContents();

  sidebar()->today_tree()->RequestFocus();
  Press(ui::VKEY_HOME);
  ASSERT_EQ(focused(), sidebar()->row_at(0));
  Press(ui::VKEY_W, /*control=*/true);

  ASSERT_TRUE(base::test::RunUntil([&] { return tabs->count() == 2; }));
  EXPECT_EQ(tabs->GetIndexOfWebContents(first), TabStripModel::kNoTab);
  // The selected tab is untouched.
  EXPECT_EQ(tabs->GetActiveWebContents(), active);
}

IN_PROC_BROWSER_TEST_F(AerieSidebarKeyboardUiTest, F6MovesFocusTopRowPillTodayThenThePage) {
  ASSERT_TRUE(base::test::RunUntil([&] { return sidebar()->row_count() == 1u; }));
  // Start in the page.
  browser()->tab_strip_model()->GetActiveWebContents()->Focus();
  ASSERT_TRUE(base::test::RunUntil([&] { return PlaceOfFocus() == Place::kElsewhere; }));

  // From the page the key goes to the renderer first, which hands it back, so
  // each press is followed by a wait until focus has moved.
  std::vector<Place> visited;
  Place previous = Place::kElsewhere;
  for (int i = 0; i < 6; ++i) {
    Press(ui::VKEY_F6);
    base::test::RunUntil([&] { return PlaceOfFocus() != previous; });
    const Place place = PlaceOfFocus();
    if (place == previous) {
      break;
    }
    visited.push_back(place);
    previous = place;
    if (place == Place::kElsewhere) {
      break;
    }
  }
  // Top row, pill, Today list, then (what is left of the toolbar, in the bottom
  // bar), then back out of the sidebar into the page.
  ASSERT_GE(visited.size(), 4u);
  EXPECT_EQ(visited[0], Place::kTopRow);
  EXPECT_EQ(visited[1], Place::kPill);
  EXPECT_EQ(visited[2], Place::kToday);
  EXPECT_NE(visited.back(), Place::kTopRow);
  EXPECT_EQ(visited.back(), Place::kElsewhere);

  // Shift+F6 goes the other way: from the top row back to the page.
  sidebar()->top_row()->RequestFocus();
  ASSERT_EQ(PlaceOfFocus(), Place::kTopRow);
  Press(ui::VKEY_F6, /*control=*/false, /*shift=*/true);
  base::test::RunUntil([&] { return PlaceOfFocus() != Place::kTopRow; });
  EXPECT_EQ(PlaceOfFocus(), Place::kElsewhere);
}
