// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

// Browser test of Clear browsing data (issue #93): the dialog opens from every
// Settings page that offers it, clears the browsing history, and closes, and
// Cancel leaves the history alone, with no crash. Run with ./aerie test-cpp
// --target aerie_browser_tests.

#include <set>
#include <string>

#include "base/run_loop.h"
#include "base/test/run_until.h"
#include "base/time/time.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/history/core/browser/history_service.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "url/gurl.h"

namespace {

// Finds an element in the page and in every shadow tree below it, the way the
// Settings page is built, and waits for it to appear.
constexpr char kDeepHelpers[] = R"(
function deep(root, selector) {
  const hit = root.querySelector(selector);
  if (hit) return hit;
  for (const element of root.querySelectorAll('*')) {
    if (element.shadowRoot) {
      const found = deep(element.shadowRoot, selector);
      if (found) return found;
    }
  }
  return null;
}
async function until(check) {
  for (let i = 0; i < 100; ++i) {
    const value = check();
    if (value) return value;
    await new Promise(r => setTimeout(r, 100));
  }
  return null;
}
)";

constexpr char kVisited[] = "https://clear-me.example/visited";

}  // namespace

class AerieClearBrowsingDataBrowserTest : public InProcessBrowserTest {
 protected:
  content::WebContents* active() {
    return browser()->tab_strip_model()->GetActiveWebContents();
  }
  content::EvalJsResult Eval(const std::string& script) {
    return content::EvalJs(active(), std::string(kDeepHelpers) + script);
  }
  void AddVisit() {
    HistoryServiceFactory::GetForProfile(browser()->GetProfile(),
                                         ServiceAccessType::EXPLICIT_ACCESS)
        ->AddPage(GURL(kVisited), base::Time::Now(), history::SOURCE_BROWSED);
    ASSERT_TRUE(base::test::RunUntil([&] { return Visited(); }));
  }
  // True when the history lists the visit.
  bool Visited() {
    ui_test_utils::HistoryEnumerator history(browser()->GetProfile());
    return std::set<GURL>(history.urls().begin(), history.urls().end())
               .count(GURL(kVisited)) > 0;
  }
  // Opens the dialog the way the Settings router does when a page links to it:
  // an in-page move to the clearBrowserData route.
  void OpenDialogFrom(const std::string& url) {
    ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), GURL(url)));
    if (url != "chrome://settings/clearBrowserData") {
      ASSERT_TRUE(content::ExecJs(
          active(),
          "history.pushState({}, '', '/clearBrowserData');"
          "window.dispatchEvent(new PopStateEvent('popstate'));"));
    }
    ASSERT_EQ(true, Eval("(async () => !!(await until(() => deep(document, "
                         "'#deleteBrowsingDataDialog'))))()"))
        << url;
  }
  // The Privacy and security page's own "Delete browsing data" row, found in
  // the shadow DOM and clicked.
  void ClickTheRow() {
    ASSERT_TRUE(ui_test_utils::NavigateToURL(
        browser(), GURL("chrome://settings/privacy")));
    ASSERT_EQ(true, Eval("(async () => { const row = await until(() => deep("
                         "document, '#clearBrowsingData')); if (!row) return "
                         "false; row.click(); return true; })()"));
    WaitForDialog();
  }
  // Ctrl+Shift+Delete, or the menu item: the command, with Settings open on
  // another route.
  void RunTheCommandFrom(const std::string& url) {
    ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), GURL(url)));
    if (url.rfind("chrome://settings", 0) == 0) {
      ASSERT_EQ(true, Eval("(async () => !!(await until(() => deep(document, "
                           "'settings-ui'))))()"));
    }
    chrome::ExecuteCommand(browser(), IDC_CLEAR_BROWSING_DATA);
    WaitForDialog();
  }
  void WaitForDialog() {
    // The command navigates a tab (this one, or a new one); wait for the page.
    ASSERT_TRUE(base::test::RunUntil([&] {
      return active()->GetLastCommittedURL().path() == "/clearBrowserData" &&
             !active()->IsLoading();
    }));
    ASSERT_EQ(true, Eval("(async () => !!(await until(() => deep(document, "
                         "'#deleteBrowsingDataDialog'))))()"));
  }
  void Press(const std::string& id) {
    ASSERT_EQ(true, Eval("(async () => { const b = await until(() => deep("
                         "document, '#" + id + "')); if (!b) return false; "
                         "b.click(); return true; })()"));
  }
  bool DialogGone() {
    return content::EvalJs(
               active(), std::string(kDeepHelpers) +
                             "!deep(document, '#deleteBrowsingDataDialog')")
        .ExtractBool();
  }
};

static const char* const kPages[] = {
    "chrome://settings/privacy",
    "chrome://settings/aerieGuard",
    "chrome://settings/search",
    "chrome://settings/clearBrowserData",
};

IN_PROC_BROWSER_TEST_F(AerieClearBrowsingDataBrowserTest,
                       ClearsTheHistoryFromEverySettingsPage) {
  for (const char* page : kPages) {
    SCOPED_TRACE(page);
    AddVisit();
    OpenDialogFrom(page);
    Press("deleteButton");
    EXPECT_TRUE(base::test::RunUntil([&] { return !Visited(); }));
    EXPECT_TRUE(base::test::RunUntil([&] { return DialogGone(); }));
    // The browser and the page are still alive.
    EXPECT_EQ(2, content::EvalJs(active(), "1 + 1"));
  }
}

IN_PROC_BROWSER_TEST_F(AerieClearBrowsingDataBrowserTest,
                       CancelLeavesTheHistoryAlone) {
  for (const char* page : kPages) {
    SCOPED_TRACE(page);
    AddVisit();
    OpenDialogFrom(page);
    Press("cancelButton");
    EXPECT_TRUE(base::test::RunUntil([&] { return DialogGone(); }));
    EXPECT_TRUE(Visited());
    EXPECT_EQ(2, content::EvalJs(active(), "1 + 1"));
  }
}

// Each way in, with the same two outcomes. `open` opens the dialog.
#define ENTRY_POINT_TESTS(name, open)                                       \
  IN_PROC_BROWSER_TEST_F(AerieClearBrowsingDataBrowserTest, name##Clears) { \
    AddVisit();                                                             \
    open;                                                                   \
    Press("deleteButton");                                                  \
    EXPECT_TRUE(base::test::RunUntil([&] { return !Visited(); }));          \
    EXPECT_TRUE(base::test::RunUntil([&] { return DialogGone(); }));        \
    EXPECT_EQ(2, content::EvalJs(active(), "1 + 1"));                       \
  }                                                                         \
  IN_PROC_BROWSER_TEST_F(AerieClearBrowsingDataBrowserTest, name##Cancels) { \
    AddVisit();                                                             \
    open;                                                                   \
    Press("cancelButton");                                                  \
    EXPECT_TRUE(base::test::RunUntil([&] { return DialogGone(); }));        \
    EXPECT_TRUE(Visited());                                                 \
    EXPECT_EQ(2, content::EvalJs(active(), "1 + 1"));                       \
  }

ENTRY_POINT_TESTS(TheDeleteBrowsingDataRow, ClickTheRow())
ENTRY_POINT_TESTS(TheCommandFromTheGuardPage,
                  RunTheCommandFrom("chrome://settings/aerieGuard"))
ENTRY_POINT_TESTS(TheCommandFromTheSearchPage,
                  RunTheCommandFrom("chrome://settings/search"))
ENTRY_POINT_TESTS(TheCommandFromAnOrdinaryPage,
                  RunTheCommandFrom("chrome://version"))
