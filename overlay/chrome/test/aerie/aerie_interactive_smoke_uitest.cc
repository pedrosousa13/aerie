// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

// The smoke test of Aerie's interactive UI test binary (issue #175). It opens a
// browser, sends a key event to its window and checks where the focus went. If
// it passes, the binary has a window that can take focus and key events, which
// every Views interaction test of Aerie needs. Run it with
// ./aerie test-cpp --target aerie_interactive_ui_tests.

#include "base/test/run_until.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/ui_features.h"
#include "chrome/browser/ui/view_ids.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/interactive_test_utils.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/test/browser_test.h"
#include "ui/events/keycodes/keyboard_codes.h"

class AerieInteractiveSmokeTest : public InProcessBrowserTest {};

IN_PROC_BROWSER_TEST_F(AerieInteractiveSmokeTest, KeyEventMovesFocus) {
  if (features::IsWebUILocationBarEnabled()) {
    GTEST_SKIP() << "The location bar is not a Views omnibox here.";
  }
  ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));

  // Start with the focus in the tab contents, not in the location bar.
  ui_test_utils::FocusView(browser(), VIEW_ID_TAB_CONTAINER);
  ASSERT_TRUE(ui_test_utils::IsViewFocused(browser(), VIEW_ID_TAB_CONTAINER));
  ASSERT_FALSE(ui_test_utils::IsViewFocused(browser(), VIEW_ID_OMNIBOX));

  // Ctrl+L is the browser's shortcut for the location bar.
  ASSERT_TRUE(ui_test_utils::SendKeyPressSync(browser(), ui::VKEY_L,
                                              /*control=*/true,
                                              /*shift=*/false, /*alt=*/false,
                                              /*command=*/false));
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return ui_test_utils::IsViewFocused(browser(), VIEW_ID_OMNIBOX);
  }));
}
