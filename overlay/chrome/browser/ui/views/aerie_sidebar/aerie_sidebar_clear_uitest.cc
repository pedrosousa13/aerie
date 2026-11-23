// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

// Interactive UI tests of the toast that Today's Clear shows (issue #266):
// "N tabs archived" with an Undo button, a screen reader hears that Undo is
// there, the button is reached with Tab and pressed with Enter, and the toast
// goes away on its own after its timeout. Run with
// ./aerie test-cpp --target aerie_interactive_ui_tests.

#include <optional>
#include <string>

#include "base/test/run_until.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_view.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/interactive_test_utils.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/test/browser_test.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/animation/animation_test_api.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/button/label_button.h"
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

class AerieSidebarClearUiTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    reduced_motion_.emplace(
        gfx::AnimationTestApi::SetPrefersReducedMotionForTesting(true));
    ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));
    host_resolver()->AddRule("*", "127.0.0.1");
    ASSERT_TRUE(embedded_test_server()->Start());
    ASSERT_TRUE(base::test::RunUntil(
        [&] { return sidebar() && sidebar()->row_count() == 1u; }));
    sidebar()->archive()->Attach();
    for (int i = 0; i < 2; ++i) {
      const size_t before = sidebar()->row_count();
      ASSERT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
          browser(), embedded_test_server()->GetURL("/title1.html"),
          WindowOpenDisposition::kNewBackgroundTab,
          ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
      ASSERT_TRUE(base::test::RunUntil(
          [&] { return sidebar()->row_count() == before + 1; }));
    }
  }
  void TearDownOnMainThread() override {
    reduced_motion_.reset();
    InProcessBrowserTest::TearDownOnMainThread();
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
  void Press(ui::KeyboardCode key) {
    ASSERT_TRUE(ui_test_utils::SendKeyPressSync(browser(), key, false, false,
                                                false, false));
  }
  void FocusView(views::View* view) {
    view->RequestFocus();
    ASSERT_TRUE(base::test::RunUntil([&] { return focused() == view; }));
  }
  // Presses Clear with the keyboard, as a user does.
  void ClearWithTheKeyboard() {
    FocusView(sidebar()->clear_button());
    Press(ui::VKEY_RETURN);
    ASSERT_TRUE(base::test::RunUntil([&] { return sidebar()->toast_visible(); }));
  }

 private:
  std::optional<gfx::AnimationTestApi::PrefersReducedMotionResetter>
      reduced_motion_;
};

IN_PROC_BROWSER_TEST_F(AerieSidebarClearUiTest,
                       TheToastSaysHowManyTabsWereArchivedAndOffersUndo) {
  ClearWithTheKeyboard();
  EXPECT_EQ(sidebar()->toast()->GetText(), u"3 tabs archived");
  // Polite for a screen reader: a status, and the announcement says Undo is
  // there.
  ui::AXNodeData data;
  sidebar()->toast()->GetViewAccessibility().GetAccessibleNodeData(&data);
  EXPECT_EQ(data.role, ax::mojom::Role::kStatus);
  EXPECT_EQ(sidebar()->last_toast_announcement_for_testing(),
            u"3 tabs archived. Undo is available.");
  ASSERT_TRUE(sidebar()->toast_undo_button()->GetVisible());
  EXPECT_EQ(sidebar()->toast_undo_button()->GetText(), u"Undo");
  EXPECT_FALSE(sidebar()->toast_dont_file_button()->GetVisible());
  EXPECT_TRUE(sidebar()->toast_undo_button()->IsFocusable());
}

IN_PROC_BROWSER_TEST_F(AerieSidebarClearUiTest,
                       UndoIsReachedWithTabAndEnterRestoresTheTabs) {
  ClearWithTheKeyboard();
  // Nothing but the new tab that keeps the window open is left in Today.
  ASSERT_EQ(sidebar()->row_count(), 1u);
  // Tab from the Clear button reaches Undo within a few stops.
  FocusView(sidebar()->clear_button());
  for (int i = 0; i < 8 && focused() != sidebar()->toast_undo_button(); ++i) {
    Press(ui::VKEY_TAB);
  }
  ASSERT_EQ(focused(), sidebar()->toast_undo_button());
  Press(ui::VKEY_RETURN);
  ASSERT_TRUE(base::test::RunUntil([&] { return sidebar()->row_count() == 4u; }));
  // The toast fades out after Undo (it is gone when the fade ends) and there is
  // nothing left to undo.
  EXPECT_TRUE(sidebar()->cleared_ids_for_testing().empty());
  ASSERT_TRUE(base::test::RunUntil([&] { return !sidebar()->toast_visible(); }));
}

IN_PROC_BROWSER_TEST_F(AerieSidebarClearUiTest, TheToastGoesAwayOnItsOwn) {
  ClearWithTheKeyboard();
  // No interaction: the toast holds for its timeout, then leaves. The wait is
  // longer than RunUntil's default so that the timeout itself is what is tested.
  ASSERT_TRUE(base::test::RunUntil([&] { return !sidebar()->toast_visible(); }));
  EXPECT_TRUE(sidebar()->cleared_ids_for_testing().empty());
  // The tabs stay archived: the toast leaving is not an Undo.
  EXPECT_EQ(sidebar()->row_count(), 1u);
}

IN_PROC_BROWSER_TEST_F(AerieSidebarClearUiTest, EnterOnTheClearButtonArchives) {
  FocusView(sidebar()->clear_button());
  Press(ui::VKEY_RETURN);
  ASSERT_TRUE(base::test::RunUntil([&] { return sidebar()->toast_visible(); }));
  EXPECT_EQ(sidebar()->cleared_ids_for_testing().size(), 3u);
}

IN_PROC_BROWSER_TEST_F(AerieSidebarClearUiTest,
                       AltEnterOnTheClearButtonClosesAndKeepsNoRecord) {
  FocusView(sidebar()->clear_button());
  ASSERT_TRUE(ui_test_utils::SendKeyPressSync(
      browser(), ui::VKEY_RETURN, /*control=*/false, /*shift=*/false,
      /*alt=*/true, /*command=*/false));
  // The three tabs are closed and the window keeps a new tab.
  ASSERT_TRUE(base::test::RunUntil([&] { return sidebar()->row_count() == 1u; }));
  EXPECT_FALSE(sidebar()->toast_visible());
  EXPECT_TRUE(sidebar()->cleared_ids_for_testing().empty());
  EXPECT_EQ(browser()->tab_strip_model()->count(), 1);
}
