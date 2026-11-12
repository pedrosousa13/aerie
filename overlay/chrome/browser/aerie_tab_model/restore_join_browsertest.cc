// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

// Browser test of the join between Today tabs and their Tab Model records across
// a restart (issue #223). PRE_ tests are the first run of a pair; the next run
// is the restart, in which Chromium's session restore brings the tabs back.

#include <map>
#include <string>

#include "base/run_loop.h"
#include "base/test/run_until.h"
#include "chrome/browser/aerie_tab_model/restore_pass.h"
#include "chrome/browser/aerie_tab_model/tab_model_service.h"
#include "chrome/browser/aerie_tab_model/tab_model_service_factory.h"
#include "chrome/browser/aerie_tab_model/tab_strip_adapter.h"
#include "chrome/browser/prefs/session_startup_pref.h"
#include "chrome/browser/lifetime/application_lifetime_desktop.h"
#include "chrome/browser/lifetime/browser_shutdown.h"
#include "chrome/browser/profiles/keep_alive/profile_keep_alive_types.h"
#include "chrome/browser/profiles/keep_alive/scoped_profile_keep_alive.h"
#include "chrome/browser/sessions/tab_restore_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/aerie_tab_model/folder_tree.h"
#include "components/aerie_tab_model/sleep_summary.h"
#include "components/aerie_tab_model/tab_model.h"
#include "components/keep_alive_registry/keep_alive_types.h"
#include "components/keep_alive_registry/scoped_keep_alive.h"
#include "components/sessions/core/tab_restore_service.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "net/test/embedded_test_server/embedded_test_server.h"

namespace aerie_tab_model {

namespace {

class LoadedWaiter : public TabModelObserver {
 public:
  explicit LoadedWaiter(TabModel* model) : model_(model) {
    model_->AddObserver(this);
  }
  ~LoadedWaiter() override { model_->RemoveObserver(this); }
  void Wait() { run_loop_.Run(); }
  void OnModelLoaded() override { run_loop_.Quit(); }

 private:
  raw_ptr<TabModel> model_;
  base::RunLoop run_loop_;
};

}  // namespace

class AerieRestoreJoinBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    ASSERT_TRUE(embedded_test_server()->Start());
    // The profile outlives the window, which some tests close.
    profile_ = browser()->GetProfile();
    service_ = TabModelServiceFactory::GetForProfile(profile_);
    ASSERT_TRUE(service_);
    if (!service_->loaded()) {
      LoadedWaiter waiter(service_->model());
      waiter.Wait();
    }
  }

  TabModel* model() { return service_->model(); }

  // Opens `path` in a new foreground tab of `target` and returns its record id.
  std::string OpenTab(BrowserWindowInterface* target, const std::string& path) {
    EXPECT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
        target, embedded_test_server()->GetURL(path),
        WindowOpenDisposition::NEW_FOREGROUND_TAB,
        ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
    TabStripModel* strip = target->GetTabStripModel();
    return service_->adapter()->RecordIdForTab(
        strip->GetTabAtIndex(strip->count() - 1));
  }

  void Name(const std::string& id, const std::string& name) {
    ASSERT_TRUE(model()->UpdateTab(
        id, [&](TabRecord& tab) { tab.manual_name = name; }));
  }

  // The window of the profile that has a tab showing `path`, or null.
  BrowserWindowInterface* WindowWithPage(const std::string& path) {
    BrowserWindowInterface* found = nullptr;
    ForEachCurrentBrowserWindowInterfaceOrderedByActivation(
        [&](BrowserWindowInterface* window) {
          if (window->GetProfile() != profile_ ||
              window->GetType() != BrowserWindowInterface::TYPE_NORMAL) {
            return true;
          }
          if (!RecordInWindow(window, path).empty()) {
            found = window;
            return false;
          }
          return true;
        });
    return found;
  }

  // The record id of the tab of `window` whose page is `path`; empty when there
  // is none. Tabs are found by address only so that the test can ask about
  // them; the join under test is by id.
  std::string RecordInWindow(BrowserWindowInterface* window,
                             const std::string& path) {
    if (!window) {
      return std::string();
    }
    TabStripModel* strip = window->GetTabStripModel();
    for (int i = 0; i < strip->count(); ++i) {
      if (strip->GetWebContentsAt(i)->GetVisibleURL().path() == path) {
        return service_->adapter()->RecordIdForTab(strip->GetTabAtIndex(i));
      }
    }
    return std::string();
  }

  size_t TabCount() {
    size_t count = 0;
    ForEachCurrentBrowserWindowInterfaceOrderedByActivation(
        [&](BrowserWindowInterface* window) {
          if (window->GetProfile() == profile_ &&
              window->GetType() == BrowserWindowInterface::TYPE_NORMAL) {
            count += window->GetTabStripModel()->count();
          }
          return true;
        });
    return count;
  }

  void WaitForRestorePass() {
    ASSERT_TRUE(base::test::RunUntil(
        [&] { return service_->restore_pass()->finished(); }));
  }

  raw_ptr<Profile> profile_ = nullptr;
  raw_ptr<TabModelService, DanglingUntriaged> service_ = nullptr;
};

// Two windows that show the SAME page, with different names, so that a join
// that crossed records between the windows would be seen. The first window has a
// folder with two named tabs; the second has a named tab. Each has an unnamed
// tab with a page of its own, so that a window can be told apart after the
// restart.
IN_PROC_BROWSER_TEST_F(AerieRestoreJoinBrowserTest, PRE_TodayTabsKeepTheirRecords) {
  SessionStartupPref::SetStartupPref(
      browser()->GetProfile(), SessionStartupPref(SessionStartupPref::LAST));
  BrowserWindowInterface* first = browser();
  BrowserWindowInterface* second = CreateBrowser(browser()->GetProfile());

  const std::string alpha = OpenTab(first, "/title1.html");
  const std::string beta = OpenTab(first, "/title2.html");
  const std::string gamma = OpenTab(second, "/title1.html");
  OpenTab(second, "/simple.html");
  ASSERT_FALSE(alpha.empty());
  ASSERT_FALSE(beta.empty());
  ASSERT_FALSE(gamma.empty());

  Name(alpha, "Alpha");
  Name(beta, "Beta");
  Name(gamma, "Gamma");
  const std::string folder =
      CreateFolder(*model(), Section::kToday, std::string(), "Work");
  ASSERT_FALSE(folder.empty());
  ASSERT_TRUE(model()->MoveTab(alpha, Section::kToday, folder, 0));
  ASSERT_TRUE(model()->MoveTab(beta, Section::kToday, folder, 1));
  ASSERT_TRUE(SetFolderExpanded(*model(), folder, false));

  // Let the ids reach the session service before the browser is shut down.
  base::RunLoop().RunUntilIdle();
}

IN_PROC_BROWSER_TEST_F(AerieRestoreJoinBrowserTest, TodayTabsKeepTheirRecords) {
  WaitForRestorePass();
  EXPECT_EQ(model()->awaiting_restore_count(), 0u);

  // The first window is the one with title2, the second the one with simple.
  BrowserWindowInterface* first = WindowWithPage("/title2.html");
  BrowserWindowInterface* second = WindowWithPage("/simple.html");
  ASSERT_TRUE(first);
  ASSERT_TRUE(second);
  ASSERT_NE(first, second);

  // The same page is open in both windows. Each tab has the record, and so the
  // name, that it had in its own window.
  const std::string alpha = RecordInWindow(first, "/title1.html");
  const std::string beta = RecordInWindow(first, "/title2.html");
  const std::string gamma = RecordInWindow(second, "/title1.html");
  ASSERT_FALSE(alpha.empty());
  ASSERT_FALSE(beta.empty());
  ASSERT_FALSE(gamma.empty());
  EXPECT_NE(alpha, gamma);
  ASSERT_TRUE(model()->GetTab(alpha));
  ASSERT_TRUE(model()->GetTab(beta));
  ASSERT_TRUE(model()->GetTab(gamma));
  EXPECT_EQ(model()->GetTab(alpha)->manual_name, "Alpha");
  EXPECT_EQ(model()->GetTab(beta)->manual_name, "Beta");
  EXPECT_EQ(model()->GetTab(gamma)->manual_name, "Gamma");

  // The two tabs of the folder keep it and their places in it; the folder is
  // still shut.
  const std::string folder_id = model()->GetTab(alpha)->parent;
  ASSERT_FALSE(folder_id.empty());
  EXPECT_EQ(model()->GetTab(beta)->parent, folder_id);
  EXPECT_EQ(model()->GetTab(alpha)->position, 0);
  EXPECT_EQ(model()->GetTab(beta)->position, 1);
  ASSERT_TRUE(model()->GetFolder(folder_id));
  EXPECT_EQ(model()->GetFolder(folder_id)->name, "Work");
  EXPECT_FALSE(model()->GetFolder(folder_id)->expanded);
  EXPECT_EQ(model()->GetTab(gamma)->parent, "");

  // The tab that had no name has a record, and there is one record per tab:
  // none is left behind and none is shared.
  const std::string plain = RecordInWindow(second, "/simple.html");
  ASSERT_FALSE(plain.empty());
  EXPECT_FALSE(model()->GetTab(plain)->manual_name);
  size_t today = 0;
  for (const auto& [id, tab] : model()->tabs()) {
    if (tab.section == Section::kToday) {
      ++today;
    }
  }
  EXPECT_EQ(today, TabCount());
}

// The last window of the profile closed with its X button: Chromium's session
// still restores it, so its tabs keep their records.
IN_PROC_BROWSER_TEST_F(AerieRestoreJoinBrowserTest, PRE_TheLastWindowClosedWithXIsRestoredWithItsRecords) {
  SessionStartupPref::SetStartupPref(
      browser()->GetProfile(), SessionStartupPref(SessionStartupPref::LAST));
  const std::string named = OpenTab(browser(), "/title1.html");
  ASSERT_FALSE(named.empty());
  Name(named, "Kept after X");
  base::RunLoop().RunUntilIdle();
  CloseBrowserSynchronously(browser());
}

IN_PROC_BROWSER_TEST_F(AerieRestoreJoinBrowserTest, TheLastWindowClosedWithXIsRestoredWithItsRecords) {
  WaitForRestorePass();
  BrowserWindowInterface* window = WindowWithPage("/title1.html");
  ASSERT_TRUE(window);
  const std::string id = RecordInWindow(window, "/title1.html");
  ASSERT_FALSE(id.empty());
  ASSERT_TRUE(model()->GetTab(id));
  EXPECT_EQ(model()->GetTab(id)->manual_name, "Kept after X");
}

// A window closed while the browser keeps running is not restored, so its
// records go with it, the sleeping ones too: no ghost records, and the count of
// sleeping tabs (the Today header) is unchanged.
IN_PROC_BROWSER_TEST_F(AerieRestoreJoinBrowserTest, ClosingAWindowMidRunRemovesItsRecords) {
  BrowserWindowInterface* second = CreateBrowser(browser()->GetProfile());
  const std::string awake = OpenTab(second, "/title1.html");
  const std::string asleep = OpenTab(second, "/title2.html");
  const std::string kept = OpenTab(browser(), "/title3.html");
  ASSERT_FALSE(awake.empty());
  ASSERT_FALSE(asleep.empty());
  ASSERT_FALSE(kept.empty());
  ASSERT_TRUE(model()->UpdateTab(
      asleep, [](TabRecord& tab) { tab.sleeping = true; }));
  ASSERT_TRUE(model()->UpdateTab(
      kept, [](TabRecord& tab) { tab.sleeping = true; }));
  // The first window's tab sleeps; the second window has one sleeping tab too.
  const int sleeping_before = CountSleepingTabs(*model());
  const int second_tabs = second->GetTabStripModel()->count();
  ASSERT_GE(second_tabs, 3);
  const size_t records_before = model()->tabs().size();

  CloseBrowserSynchronously(second);

  EXPECT_EQ(model()->GetTab(awake), nullptr);
  EXPECT_EQ(model()->GetTab(asleep), nullptr);
  ASSERT_TRUE(model()->GetTab(kept));
  EXPECT_EQ(model()->tabs().size(), records_before - static_cast<size_t>(second_tabs));
  // The sleeping tab of the closed window is gone from the count; the other
  // one stays. (Before: two sleeping tabs; after: one.)
  EXPECT_EQ(sleeping_before, 2);
  EXPECT_EQ(CountSleepingTabs(*model()), 1);
  // Nothing waits for a claim either.
  EXPECT_EQ(model()->awaiting_restore_count(), 0u);
}

// The last window closed while a keep-alive holds the process (macOS always;
// Linux with a background app): nothing restores the tab, so its record waits
// only until the next window opens. A window that is not a restore ends the wait
// and the record is removed.
IN_PROC_BROWSER_TEST_F(AerieRestoreJoinBrowserTest, TheLastWindowUnderAKeepAliveKeepsItsRecordsOnlyUntilTheNextWindow) {
  Profile* profile = browser()->GetProfile();
  ScopedKeepAlive keep_alive(KeepAliveOrigin::SESSION_RESTORE,
                             KeepAliveRestartOption::DISABLED);
  ScopedProfileKeepAlive profile_keep_alive(
      profile, ProfileKeepAliveOrigin::kBrowserWindow);
  const std::string id = OpenTab(browser(), "/title1.html");
  ASSERT_FALSE(id.empty());
  Name(id, "Waiting");

  CloseBrowserSynchronously(browser());

  // The tab is gone; the record waits, and no row or count can show it.
  ASSERT_TRUE(model()->GetTab(id));
  EXPECT_TRUE(model()->IsAwaitingRestore(id));
  EXPECT_EQ(service_->adapter()->TabForRecord(id), nullptr);
  EXPECT_EQ(CountSleepingTabs(*model()), 0);

  // A new window that is not a restore: the wait is over.
  CreateBrowser(profile);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return service_->restore_pass()->finished(); }));
  EXPECT_EQ(model()->GetTab(id), nullptr);
  EXPECT_EQ(model()->awaiting_restore_count(), 0u);
}

// A quit that is cancelled (a beforeunload prompt, a download) leaves the
// windows open: the records kept while it ran must not stay as ghosts.
IN_PROC_BROWSER_TEST_F(AerieRestoreJoinBrowserTest, ACancelledQuitEndsTheWaitForKeptRecords) {
  Profile* profile = browser()->GetProfile();
  ScopedKeepAlive keep_alive(KeepAliveOrigin::SESSION_RESTORE,
                             KeepAliveRestartOption::DISABLED);
  ScopedProfileKeepAlive profile_keep_alive(
      profile, ProfileKeepAliveOrigin::kBrowserWindow);
  const std::string id = OpenTab(browser(), "/title1.html");
  ASSERT_FALSE(id.empty());

  browser_shutdown::SetTryingToQuit(true);
  CloseBrowserSynchronously(browser());
  ASSERT_TRUE(model()->IsAwaitingRestore(id));

  // A window exists again, but the quit is still on: the wait goes on.
  CreateBrowser(profile);
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(service_->restore_pass()->finished());
  EXPECT_TRUE(model()->IsAwaitingRestore(id));

  // The quit is cancelled: the same signal Chromium sends from UnloadController.
  browser_shutdown::SetTryingToQuit(false);
  chrome::OnClosingAllBrowsers(false);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return service_->restore_pass()->finished(); }));
  EXPECT_EQ(model()->GetTab(id), nullptr);
  EXPECT_EQ(model()->awaiting_restore_count(), 0u);
}

// The same, but the window is reopened (Ctrl+Shift+T): its tabs carry their old
// record ids, claim the waiting records, and keep their names.
IN_PROC_BROWSER_TEST_F(AerieRestoreJoinBrowserTest, ReopeningTheLastClosedWindowGivesItsTabsTheirRecordsBack) {
  Profile* profile = browser()->GetProfile();
  ScopedKeepAlive keep_alive(KeepAliveOrigin::SESSION_RESTORE,
                             KeepAliveRestartOption::DISABLED);
  ScopedProfileKeepAlive profile_keep_alive(
      profile, ProfileKeepAliveOrigin::kBrowserWindow);
  const std::string named = OpenTab(browser(), "/title1.html");
  const std::string other = OpenTab(browser(), "/title2.html");
  ASSERT_FALSE(named.empty());
  ASSERT_FALSE(other.empty());
  Name(named, "Reopened");
  base::RunLoop().RunUntilIdle();

  CloseBrowserSynchronously(browser());
  ASSERT_TRUE(model()->IsAwaitingRestore(named));

  sessions::TabRestoreService* restore =
      TabRestoreServiceFactory::GetForProfile(profile);
  ASSERT_TRUE(restore);
  ASSERT_FALSE(restore->entries().empty());
  restore->RestoreMostRecentEntry(nullptr);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return WindowWithPage("/title1.html") != nullptr; }));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return service_->restore_pass()->finished(); }));

  BrowserWindowInterface* window = WindowWithPage("/title1.html");
  ASSERT_TRUE(window);
  // The same records, claimed by the reopened tabs.
  EXPECT_EQ(RecordInWindow(window, "/title1.html"), named);
  EXPECT_EQ(RecordInWindow(window, "/title2.html"), other);
  ASSERT_TRUE(model()->GetTab(named));
  EXPECT_EQ(model()->GetTab(named)->manual_name, "Reopened");
  EXPECT_EQ(model()->awaiting_restore_count(), 0u);
}

}  // namespace aerie_tab_model
