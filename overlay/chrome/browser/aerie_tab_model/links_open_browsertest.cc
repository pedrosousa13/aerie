// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

// Browser tests of "Links open" (issue #141): a link that would open a new tab
// opens in a new tab or in place, by the nearest folder that says so or by
// Settings, and Ctrl+click, middle-click, Shift+click, pop-up windows,
// window.open that keeps its opener and links in embedded frames are left
// alone. One test per case.

#include <memory>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/run_loop.h"
#include "base/strings/stringprintf.h"
#include "base/test/run_until.h"
#include "chrome/browser/aerie_tab_model/tab_model_service.h"
#include "chrome/browser/aerie_tab_model/tab_model_service_factory.h"
#include "chrome/browser/aerie_tab_model/tab_strip_adapter.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/aerie_tab_model/folder_behaviour.h"
#include "components/aerie_tab_model/folder_tree.h"
#include "components/aerie_tab_model/tab_model.h"
#include "components/aerie_tab_model/tab_model_prefs.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/test_navigation_observer.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "third_party/blink/public/common/input/web_input_event.h"
#include "third_party/blink/public/common/input/web_mouse_event.h"
#include "url/gurl.h"

namespace aerie_tab_model {

namespace {

// The page every test clicks in. Each control sits on its own 40 px line.
constexpr char kLinksPage[] = R"HTML(<!doctype html><html><body style="margin:0">
<a id="blank" href="/title2.html" target="_blank" style="display:block;height:40px">blank</a>
<a id="same" href="/title2.html" style="display:block;height:40px">same</a>
<a id="opener" href="/title2.html" target="_blank" rel="opener" style="display:block;height:40px">opener</a>
<button id="popup" style="display:block;height:40px"
  onclick="window.open('/title2.html', 'p', 'popup=yes,width=300,height=300')">popup</button>
<button id="open" style="display:block;height:40px"
  onclick="window.open('/title2.html')">open</button>
<iframe id="frame" width="300" height="60" src="/frame_with_link.html"></iframe>
</body></html>)HTML";

// The same kind of page with one frame that is sandboxed against navigating the
// top level (allow-popups lets it open a tab, but not take the tab over).
constexpr char kSandboxPage[] = R"HTML(<!doctype html><html><body style="margin:0">
<a id="blank" href="/title2.html" target="_blank" style="display:block;height:40px">blank</a>
<iframe id="sandbox" width="300" height="60" sandbox="allow-popups allow-scripts"
  src="/frame_with_link.html"></iframe>
</body></html>)HTML";

constexpr char kFramePage[] = R"HTML(<!doctype html><html><body style="margin:0">
<a id="inner" href="/title2.html" target="_blank" style="display:block;height:40px">inner</a>
</body></html>)HTML";

std::unique_ptr<net::test_server::HttpResponse> ServePages(
    const net::test_server::HttpRequest& request) {
  const char* body = nullptr;
  if (request.relative_url == "/links.html") {
    body = kLinksPage;
  } else if (request.relative_url == "/sandbox.html") {
    body = kSandboxPage;
  } else if (request.relative_url == "/frame_with_link.html") {
    body = kFramePage;
  } else {
    return nullptr;
  }
  auto response = std::make_unique<net::test_server::BasicHttpResponse>();
  response->set_content_type("text/html");
  response->set_content(body);
  return response;
}

}  // namespace

class AerieLinksOpenBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    host_resolver()->AddRule("*", "127.0.0.1");
    embedded_test_server()->RegisterRequestHandler(
        base::BindRepeating(&ServePages));
    ASSERT_TRUE(embedded_test_server()->Start());
    service_ = TabModelServiceFactory::GetForProfile(browser()->GetProfile());
    ASSERT_TRUE(service_);
    ASSERT_TRUE(ui_test_utils::NavigateToURL(
        browser(), embedded_test_server()->GetURL("/links.html")));
    ASSERT_TRUE(content::WaitForLoadStop(contents()));
  }

  TabStripModel* strip() { return browser()->tab_strip_model(); }
  content::WebContents* contents() { return strip()->GetWebContentsAt(0); }
  TabModel* model() { return service_->model(); }
  PrefService* prefs() { return browser()->GetProfile()->GetPrefs(); }
  GURL Target() { return embedded_test_server()->GetURL("/title2.html"); }

  std::string RecordIdAt(int index) {
    return service_->adapter()->RecordIdForTab(strip()->GetTabAtIndex(index));
  }

  // Puts the tab of the page into a new Today folder and returns its id.
  std::string MovePageTabIntoFolder() {
    const std::string folder =
        CreateFolder(*model(), Section::kToday, std::string(), "Reading");
    EXPECT_TRUE(MoveTabToFolder(*model(), RecordIdAt(0), folder));
    return folder;
  }

  // A click on the element in the main frame, sent from the page with a user
  // gesture: plain, with `modifiers`, or the middle button (an "auxclick").
  void Click(const char* id,
             int modifiers = blink::WebInputEvent::kNoModifiers,
             blink::WebMouseEvent::Button button =
                 blink::WebMouseEvent::Button::kLeft) {
    const bool middle = button == blink::WebMouseEvent::Button::kMiddle;
    const std::string script = base::StringPrintf(
        "(() => { const e = document.getElementById('%s');"
        " e.dispatchEvent(new MouseEvent('%s', {bubbles: true,"
        " cancelable: true, view: window, button: %d, ctrlKey: %s,"
        " shiftKey: %s})); return true; })()",
        id, middle ? "auxclick" : "click", middle ? 1 : 0,
        (modifiers & blink::WebInputEvent::kControlKey) ? "true" : "false",
        (modifiers & blink::WebInputEvent::kShiftKey) ? "true" : "false");
    ASSERT_EQ(true, content::EvalJs(contents(), script));
  }

  // A click on the link inside an embedded frame, which takes focus first as a
  // real click on it would.
  void ClickFrame(const char* frame_id) {
    ASSERT_TRUE(content::ExecJs(
        contents(), base::StringPrintf(
                        "document.getElementById('%s').contentWindow.focus()",
                        frame_id)));
    ASSERT_TRUE(base::test::RunUntil([&] {
      return contents()->GetFocusedFrame() != contents()->GetPrimaryMainFrame();
    }));
    content::RenderFrameHost* child =
        content::ChildFrameAt(contents()->GetPrimaryMainFrame(), 0);
    ASSERT_TRUE(child);
    ASSERT_TRUE(content::ExecJs(
        child, "document.getElementById('inner').click()"));
  }

  // Clicks and waits for the tab to navigate in place.
  void ClickAndExpectInPlace(const char* id) {
    content::TestNavigationObserver observer(contents());
    Click(id);
    observer.Wait();
    EXPECT_EQ(strip()->count(), 1);
    EXPECT_EQ(contents()->GetLastCommittedURL(), Target());
  }

  // Clicks and waits for one new tab in this window.
  content::WebContents* ClickAndExpectNewTab(
      const char* id,
      int modifiers = blink::WebInputEvent::kNoModifiers,
      blink::WebMouseEvent::Button button =
          blink::WebMouseEvent::Button::kLeft) {
    ui_test_utils::TabAddedWaiter waiter(browser());
    Click(id, modifiers, button);
    content::WebContents* added = waiter.Wait();
    EXPECT_EQ(strip()->count(), 2);
    return added;
  }

  raw_ptr<TabModelService> service_ = nullptr;
};

// --- In place ---

IN_PROC_BROWSER_TEST_F(AerieLinksOpenBrowserTest,
                       InPlaceOpensATargetBlankLinkInTheSameTab) {
  const std::string folder = MovePageTabIntoFolder();
  ASSERT_TRUE(SetFolderLinksOpen(*model(), folder, LinksOpen::kInPlace));
  const std::string record = RecordIdAt(0);
  ClickAndExpectInPlace("blank");
  // It is the same tab, in the same folder.
  EXPECT_EQ(RecordIdAt(0), record);
  EXPECT_EQ(model()->GetTab(record)->parent, folder);
  // Back goes to the page that held the link.
  EXPECT_TRUE(contents()->GetController().CanGoBack());
}

IN_PROC_BROWSER_TEST_F(AerieLinksOpenBrowserTest,
                       InPlaceLeavesAnOrdinaryLinkAlone) {
  const std::string folder = MovePageTabIntoFolder();
  ASSERT_TRUE(SetFolderLinksOpen(*model(), folder, LinksOpen::kInPlace));
  ClickAndExpectInPlace("same");
}

IN_PROC_BROWSER_TEST_F(AerieLinksOpenBrowserTest,
                       InPlaceFollowsTheNearestFolderAboveTheTab) {
  const std::string outer =
      CreateFolder(*model(), Section::kToday, std::string(), "Outer");
  const std::string inner =
      CreateFolder(*model(), Section::kToday, outer, "Inner");
  ASSERT_TRUE(MoveTabToFolder(*model(), RecordIdAt(0), inner));
  ASSERT_TRUE(SetFolderLinksOpen(*model(), outer, LinksOpen::kInPlace));
  // The inner folder sets nothing: the outer one applies.
  ClickAndExpectInPlace("blank");
}

// --- In a new tab ---

IN_PROC_BROWSER_TEST_F(AerieLinksOpenBrowserTest,
                       NewTabOpensANewTabNextToTheSourceInsideTheFolder) {
  // Settings say in place; the folder says in a new tab, and wins.
  prefs()->SetString(kLinksOpen, "in-place");
  const std::string folder = MovePageTabIntoFolder();
  ASSERT_TRUE(SetFolderLinksOpen(*model(), folder, LinksOpen::kNewTab));
  // A sibling that sits after the source in the folder.
  TabRecord sibling;
  sibling.section = Section::kToday;
  sibling.parent = folder;
  sibling.position = 5;
  const std::string sibling_id = model()->AddTab(std::move(sibling));
  const std::string source = RecordIdAt(0);

  content::WebContents* added = ClickAndExpectNewTab("blank");
  EXPECT_EQ(contents()->GetLastCommittedURL(),
            embedded_test_server()->GetURL("/links.html"));
  const std::string added_id = service_->adapter()->RecordIdForTab(
      strip()->GetTabAtIndex(strip()->GetIndexOfWebContents(added)));
  ASSERT_FALSE(added_id.empty());
  const TabRecord* record = model()->GetTab(added_id);
  EXPECT_EQ(record->parent, folder);
  // Right after the source and before the sibling that was after it.
  EXPECT_EQ(model()->GetTab(source)->position + 1, record->position);
  EXPECT_LT(record->position, model()->GetTab(sibling_id)->position);
}

IN_PROC_BROWSER_TEST_F(AerieLinksOpenBrowserTest,
                       TheGlobalNewTabValueDoesNotMoveTheNewTabIntoAFolder) {
  MovePageTabIntoFolder();
  // The folder sets nothing and Settings say "new tab" (the default).
  content::WebContents* added = ClickAndExpectNewTab("blank");
  const std::string added_id = service_->adapter()->RecordIdForTab(
      strip()->GetTabAtIndex(strip()->GetIndexOfWebContents(added)));
  ASSERT_FALSE(added_id.empty());
  EXPECT_EQ(model()->GetTab(added_id)->parent, std::string());
}

// --- Settings ---

IN_PROC_BROWSER_TEST_F(AerieLinksOpenBrowserTest,
                       AFolderThatInheritsFollowsSettingsAtOnce) {
  const std::string folder = MovePageTabIntoFolder();
  prefs()->SetString(kLinksOpen, "in-place");
  ClickAndExpectInPlace("blank");
  // Settings change back: the next click opens a tab again, with no restart.
  prefs()->SetString(kLinksOpen, "new-tab");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/links.html")));
  ClickAndExpectNewTab("blank");
  EXPECT_FALSE(model()->GetFolder(folder)->behaviour);
}

IN_PROC_BROWSER_TEST_F(AerieLinksOpenBrowserTest,
                       SettingsApplyToATabThatIsInNoFolder) {
  prefs()->SetString(kLinksOpen, "in-place");
  ClickAndExpectInPlace("blank");
}

// --- Left alone ---

// The control for the two tests below: the same request made where the hook
// does not act (a tab in no folder, Settings say "new tab") and where it would
// (a folder that says "in place"). The hook must leave it exactly as it is, so
// the two runs must agree on everything that can be seen.
IN_PROC_BROWSER_TEST_F(AerieLinksOpenBrowserTest,
                       CtrlClickIsTheSameWithAndWithoutAFolderThatSaysInPlace) {
  struct Outcome {
    int count;
    int active;
    GURL page;
  };
  const auto run = [&]() {
    ui_test_utils::TabAddedWaiter waiter(browser());
    Click("same", blink::WebInputEvent::kControlKey);
    waiter.Wait();
    return Outcome{strip()->count(), strip()->active_index(),
                   contents()->GetLastCommittedURL()};
  };
  // Control: no folder, Settings "new tab". The hook has nothing to do.
  const Outcome control = run();
  EXPECT_EQ(control.count, 2);
  strip()->CloseWebContentsAt(1, TabCloseTypes::CLOSE_NONE);
  ASSERT_EQ(strip()->count(), 1);
  strip()->ActivateTabAt(0);
  // The hook would act: the folder, and Settings, say "in place".
  prefs()->SetString(kLinksOpen, "in-place");
  const std::string folder = MovePageTabIntoFolder();
  ASSERT_TRUE(SetFolderLinksOpen(*model(), folder, LinksOpen::kInPlace));
  const Outcome with_hook = run();
  EXPECT_EQ(with_hook.count, control.count);
  EXPECT_EQ(with_hook.active, control.active)
      << "the hook changed which tab is active";
  EXPECT_EQ(with_hook.page, control.page);
  EXPECT_EQ(with_hook.page, embedded_test_server()->GetURL("/links.html"));
}

// A middle click reaches Navigate as the same request as a Ctrl+click: a
// background tab from a link. A headless run cannot send the mouse button (a
// simulated mouse click never reaches the page), so the request is made at the
// place the hook sits, chrome's Navigate, as the browser makes it for a middle
// click. NavigateParams has no field for the button: the disposition is all
// that tells the cases apart.
IN_PROC_BROWSER_TEST_F(AerieLinksOpenBrowserTest,
                       ABackgroundTabRequestIsTheSameWithAndWithoutAFolder) {
  const auto run = [&]() {
    NavigateParams params(browser(), Target(), ui::PAGE_TRANSITION_LINK);
    params.disposition = WindowOpenDisposition::NEW_BACKGROUND_TAB;
    params.source_contents = contents();
    params.user_gesture = true;
    params.original_user_gesture = true;
    ui_test_utils::TabAddedWaiter waiter(browser());
    Navigate(&params);
    waiter.Wait();
    return std::make_pair(strip()->count(), strip()->active_index());
  };
  const auto control = run();
  EXPECT_EQ(control.first, 2);
  // A real background tab leaves the page's tab selected.
  EXPECT_EQ(control.second, 0);
  strip()->CloseWebContentsAt(1, TabCloseTypes::CLOSE_NONE);
  ASSERT_EQ(strip()->count(), 1);
  prefs()->SetString(kLinksOpen, "in-place");
  const std::string folder = MovePageTabIntoFolder();
  ASSERT_TRUE(SetFolderLinksOpen(*model(), folder, LinksOpen::kInPlace));
  const auto with_hook = run();
  EXPECT_EQ(with_hook, control);
  EXPECT_EQ(contents()->GetLastCommittedURL(),
            embedded_test_server()->GetURL("/links.html"));
}

IN_PROC_BROWSER_TEST_F(AerieLinksOpenBrowserTest,
                       ShiftClickStillOpensANewWindow) {
  prefs()->SetString(kLinksOpen, "in-place");
  ui_test_utils::BrowserCreatedObserver created;
  Click("same", blink::WebInputEvent::kShiftKey);
  BrowserWindowInterface* other = created.Wait();
  ASSERT_TRUE(other);
  EXPECT_NE(other, browser());
  EXPECT_EQ(contents()->GetLastCommittedURL(),
            embedded_test_server()->GetURL("/links.html"));
}

IN_PROC_BROWSER_TEST_F(AerieLinksOpenBrowserTest,
                       AWindowOpenWithFeaturesStillOpensAPopup) {
  prefs()->SetString(kLinksOpen, "in-place");
  ui_test_utils::BrowserCreatedObserver created;
  Click("popup");
  BrowserWindowInterface* other = created.Wait();
  ASSERT_TRUE(other);
  EXPECT_NE(other, browser());
  EXPECT_EQ(strip()->count(), 1);
  EXPECT_EQ(contents()->GetLastCommittedURL(),
            embedded_test_server()->GetURL("/links.html"));
}

IN_PROC_BROWSER_TEST_F(AerieLinksOpenBrowserTest,
                       AWindowOpenThatKeepsItsOpenerStillOpensATab) {
  prefs()->SetString(kLinksOpen, "in-place");
  content::WebContents* added = ClickAndExpectNewTab("open");
  // The new tab can still reach the page through window.opener.
  EXPECT_EQ(true, content::EvalJs(added, "window.opener !== null"));
  EXPECT_EQ(contents()->GetLastCommittedURL(),
            embedded_test_server()->GetURL("/links.html"));
}

IN_PROC_BROWSER_TEST_F(AerieLinksOpenBrowserTest,
                       ATargetBlankLinkThatKeepsItsOpenerStillOpensATab) {
  prefs()->SetString(kLinksOpen, "in-place");
  ClickAndExpectNewTab("opener");
  EXPECT_EQ(contents()->GetLastCommittedURL(),
            embedded_test_server()->GetURL("/links.html"));
}

IN_PROC_BROWSER_TEST_F(AerieLinksOpenBrowserTest,
                       ALinkInAnEmbeddedFrameStillOpensANewTab) {
  prefs()->SetString(kLinksOpen, "in-place");
  ui_test_utils::TabAddedWaiter waiter(browser());
  // The frame that is clicked takes focus, so the click is not the page's own.
  ClickFrame("frame");
  waiter.Wait();
  EXPECT_EQ(strip()->count(), 2);
  EXPECT_EQ(contents()->GetLastCommittedURL(),
            embedded_test_server()->GetURL("/links.html"));
}

IN_PROC_BROWSER_TEST_F(AerieLinksOpenBrowserTest,
                       APageWithAFrameSandboxedAgainstTopNavigationKeepsItsTab) {
  prefs()->SetString(kLinksOpen, "in-place");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/sandbox.html")));
  ui_test_utils::TabAddedWaiter waiter(browser());
  // Even a click on the page's own link opens a tab while a frame of the page
  // is sandboxed: the browser cannot tell which frame the click came from.
  Click("blank");
  waiter.Wait();
  EXPECT_EQ(strip()->count(), 2);
  EXPECT_EQ(contents()->GetLastCommittedURL(),
            embedded_test_server()->GetURL("/sandbox.html"));
}

}  // namespace aerie_tab_model
