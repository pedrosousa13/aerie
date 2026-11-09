// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

// The isolation of the Guard panel (issue #100): no renderer of an ordinary
// page can bind its Mojo interface, no web page can navigate to it, and the
// page has no chrome.send.

#include "chrome/browser/chrome_browser_interface_binders.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/webui/aerie_guard/aerie_guard.mojom.h"
#include "chrome/common/webui_url_constants.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "mojo/public/cpp/bindings/binder_map.h"
#include "mojo/public/cpp/bindings/generic_pending_receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "url/gurl.h"

namespace {

class AerieGuardBrowserTest : public InProcessBrowserTest {
 public:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    host_resolver()->AddRule("*", "127.0.0.1");
    ASSERT_TRUE(embedded_test_server()->Start());
  }

  content::WebContents* web_contents() {
    return browser()->tab_strip_model()->GetActiveWebContents();
  }
};

// The binders every frame gets (the ones a normal renderer can reach) do not
// include the panel's interfaces. They are bound only for the panel's own WebUI
// (chrome_browser_interface_binders_webui_parts_desktop.cc).
IN_PROC_BROWSER_TEST_F(AerieGuardBrowserTest,
                       AnOrdinaryFrameCannotBindThePanelsInterfaces) {
  ASSERT_TRUE(content::NavigateToURL(
      web_contents(), embedded_test_server()->GetURL("news.test", "/title1.html")));
  content::RenderFrameHost* frame = web_contents()->GetPrimaryMainFrame();

  mojo::BinderMapWithContext<content::RenderFrameHost*> binders;
  chrome::internal::PopulateChromeFrameBinders(&binders, frame);

  mojo::Remote<aerie_guard::mojom::PageHandlerFactory> factory;
  mojo::GenericPendingReceiver factory_receiver(
      factory.BindNewPipeAndPassReceiver());
  EXPECT_FALSE(binders.TryBind(frame, &factory_receiver));

  // The handler and the page are never bound by name, either.
  mojo::Remote<aerie_guard::mojom::PageHandler> handler;
  mojo::GenericPendingReceiver handler_receiver(
      handler.BindNewPipeAndPassReceiver());
  EXPECT_FALSE(binders.TryBind(frame, &handler_receiver));
}

// A web page cannot navigate to chrome://aerie-guard.top-chrome/: not by a
// script, and not by a link.
IN_PROC_BROWSER_TEST_F(AerieGuardBrowserTest,
                       AWebPageCannotNavigateToThePanel) {
  const GURL page = embedded_test_server()->GetURL("news.test", "/title1.html");
  ASSERT_TRUE(content::NavigateToURL(web_contents(), page));

  EXPECT_TRUE(content::ExecJs(web_contents(),
                              "location.href = '" +
                                  std::string(chrome::kChromeUIAerieGuardURL) +
                                  "';"));
  content::RunAllTasksUntilIdle();
  EXPECT_EQ(page, web_contents()->GetLastCommittedURL());
  EXPECT_EQ(page, web_contents()->GetPrimaryMainFrame()->GetLastCommittedURL());

  EXPECT_TRUE(content::ExecJs(web_contents(),
                              "const a = document.createElement('a');"
                              "a.href = '" +
                                  std::string(chrome::kChromeUIAerieGuardURL) +
                                  "'; document.body.appendChild(a); a.click();"));
  content::RunAllTasksUntilIdle();
  EXPECT_EQ(page, web_contents()->GetLastCommittedURL());
}

// Loaded straight into a tab (no bubble, so no captured tab) the page has no
// chrome.send, and its handler has nothing to act on.
IN_PROC_BROWSER_TEST_F(AerieGuardBrowserTest, TheDirectPageHasNoChromeSend) {
  ASSERT_TRUE(content::NavigateToURL(
      web_contents(), GURL(chrome::kChromeUIAerieGuardURL)));
  EXPECT_EQ(
      "undefined",
      content::EvalJs(web_contents(),
                      "typeof (window.chrome && window.chrome.send)"));
}

}  // namespace
