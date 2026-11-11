// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

// The Guard panel opens from the address pill in the sidebar (issue #215): the
// test clicks the Guard icon in the sidebar with a real mouse event, and the
// panel's widget appears, and Escape closes it again. Run it with ./aerie test-cpp --target aerie_interactive_ui_tests.

#include <string>

#include "base/test/run_until.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/public/tab_features.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/aerie_adblock/aerie_guard_page_action_controller.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_view.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"
#include "chrome/common/pref_names.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/interactive_test_utils.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/prefs/pref_service.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/base/test/ui_controls.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/view.h"
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

// The visible view under `root` that names itself "Guard: ...".
views::View* FindGuardIcon(views::View* root) {
  ui::AXNodeData data;
  root->GetViewAccessibility().GetAccessibleNodeData(&data);
  if (root->GetVisible() && root->width() > 0 &&
      data.GetStringAttribute(ax::mojom::StringAttribute::kName)
              .rfind("Guard:", 0) == 0) {
    return root;
  }
  for (views::View* child : root->children()) {
    if (views::View* found = FindGuardIcon(child)) {
      return found;
    }
  }
  return nullptr;
}

}  // namespace

class AerieSidebarGuardUiTest : public InProcessBrowserTest {
 public:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    // HTTPS-only mode is on by default in Aerie and would upgrade the page.
    browser()->GetProfile()->GetPrefs()->SetBoolean(prefs::kHttpsOnlyModeEnabled,
                                                 false);
    host_resolver()->AddRule("*", "127.0.0.1");
    embedded_test_server()->ServeFilesFromSourceDirectory("content/test/data");
    ASSERT_TRUE(embedded_test_server()->Start());
  }

  content::WebContents* web_contents() {
    return browser()->tab_strip_model()->GetActiveWebContents();
  }

  AerieGuardPageActionController* guard() {
    return tabs::TabInterface::GetFromContents(web_contents())
        ->GetTabFeatures()
        ->aerie_guard_page_action_controller();
  }

  // A real left click in the middle of `view`.
  void Click(views::View* view) {
    const gfx::Point center = view->GetBoundsInScreen().CenterPoint();
    ASSERT_TRUE(ui_test_utils::SendMouseMoveSync(center));
    ASSERT_TRUE(ui_test_utils::SendMouseEventsSync(
        ui_controls::LEFT, ui_controls::DOWN | ui_controls::UP));
  }
};

IN_PROC_BROWSER_TEST_F(AerieSidebarGuardUiTest, TheGuardIconInThePillOpensThePanel) {
  ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));
  ASSERT_TRUE(content::NavigateToURL(
      web_contents(),
      embedded_test_server()->GetURL("news.test", "/title1.html")));

  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  auto* sidebar = FindDescendant<AerieSidebarView>(
      browser_view->vertical_tab_strip_region_view_for_testing());
  ASSERT_TRUE(sidebar);

  // The icon is in the sidebar's address pill, not in a toolbar above the page.
  views::View* icon = nullptr;
  ASSERT_TRUE(base::test::RunUntil([&] {
    icon = FindGuardIcon(sidebar->address_pill());
    return icon != nullptr;
  }));
  ASSERT_TRUE(sidebar->address_pill()->Contains(icon));
  EXPECT_FALSE(guard()->panel_widget_for_testing());

  Click(icon);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return guard()->panel_widget_for_testing() != nullptr; }));
  // The panel is a separate widget; it shows once its page has loaded.
  ASSERT_TRUE(base::test::RunUntil([&] {
    return guard()->panel_widget_for_testing() &&
           guard()->panel_widget_for_testing()->IsVisible();
  }));

  // Escape closes it, as a user closes it.
  ASSERT_TRUE(ui_test_utils::SendKeyPressSync(browser(), ui::VKEY_ESCAPE,
                                              /*control=*/false,
                                              /*shift=*/false, /*alt=*/false,
                                              /*command=*/false));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return guard()->panel_widget_for_testing() == nullptr; }));
}
