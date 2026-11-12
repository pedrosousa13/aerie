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
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/aerie_tab_model/folder_tree.h"
#include "components/aerie_tab_model/tab_model.h"
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
    service_ = TabModelServiceFactory::GetForProfile(browser()->GetProfile());
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

  // The record id of the restored tab whose page is `path`, over all windows of
  // the profile; empty when there is none. The tab is found by its address only
  // so that the test can ask about it. The join under test is by id.
  std::string RecordOfPage(const std::string& path) {
    std::string found;
    ForEachCurrentBrowserWindowInterfaceOrderedByActivation(
        [&](BrowserWindowInterface* window) {
          if (window->GetProfile() != browser()->GetProfile() ||
              window->GetType() != BrowserWindowInterface::TYPE_NORMAL) {
            return true;
          }
          TabStripModel* strip = window->GetTabStripModel();
          for (int i = 0; i < strip->count(); ++i) {
            if (strip->GetWebContentsAt(i)->GetVisibleURL().path() == path) {
              found = service_->adapter()->RecordIdForTab(strip->GetTabAtIndex(i));
            }
          }
          return true;
        });
    return found;
  }

  size_t TabCount() {
    size_t count = 0;
    ForEachCurrentBrowserWindowInterfaceOrderedByActivation(
        [&](BrowserWindowInterface* window) {
          if (window->GetProfile() == browser()->GetProfile() &&
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

  raw_ptr<TabModelService, DanglingUntriaged> service_ = nullptr;
};

// Two windows. In the first, a named tab inside a folder, and a named tab at the
// top level; in the second, a named tab. A fourth tab has no name.
IN_PROC_BROWSER_TEST_F(AerieRestoreJoinBrowserTest, PRE_TodayTabsKeepTheirRecords) {
  SessionStartupPref::SetStartupPref(
      browser()->GetProfile(), SessionStartupPref(SessionStartupPref::LAST));
  BrowserWindowInterface* first = browser();
  BrowserWindowInterface* second = CreateBrowser(browser()->GetProfile());

  const std::string alpha = OpenTab(first, "/title1.html");
  const std::string beta = OpenTab(first, "/title2.html");
  const std::string gamma = OpenTab(second, "/title3.html");
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
  ASSERT_TRUE(SetFolderExpanded(*model(), folder, false));

  // Let the ids reach the session service before the browser is shut down.
  base::RunLoop().RunUntilIdle();
}

IN_PROC_BROWSER_TEST_F(AerieRestoreJoinBrowserTest, TodayTabsKeepTheirRecords) {
  WaitForRestorePass();
  // Two windows came back, with the tabs of both, and nothing is left over.
  EXPECT_EQ(model()->awaiting_restore_count(), 0u);

  const std::string alpha = RecordOfPage("/title1.html");
  const std::string beta = RecordOfPage("/title2.html");
  const std::string gamma = RecordOfPage("/title3.html");
  ASSERT_FALSE(alpha.empty());
  ASSERT_FALSE(beta.empty());
  ASSERT_FALSE(gamma.empty());
  ASSERT_TRUE(model()->GetTab(alpha));
  ASSERT_TRUE(model()->GetTab(beta));
  ASSERT_TRUE(model()->GetTab(gamma));

  // Each tab is joined to its own record: its name, and for the first its
  // folder (which is still shut).
  EXPECT_EQ(model()->GetTab(alpha)->manual_name, "Alpha");
  EXPECT_EQ(model()->GetTab(beta)->manual_name, "Beta");
  EXPECT_EQ(model()->GetTab(gamma)->manual_name, "Gamma");
  const std::string folder_id = model()->GetTab(alpha)->parent;
  ASSERT_FALSE(folder_id.empty());
  ASSERT_TRUE(model()->GetFolder(folder_id));
  EXPECT_EQ(model()->GetFolder(folder_id)->name, "Work");
  EXPECT_FALSE(model()->GetFolder(folder_id)->expanded);
  EXPECT_EQ(model()->GetTab(alpha)->position, 0);
  EXPECT_EQ(model()->GetTab(beta)->parent, "");

  // The tab that had no name has a record, and there is one record per tab:
  // none is left behind and none is shared.
  const std::string plain = RecordOfPage("/simple.html");
  ASSERT_FALSE(plain.empty());
  EXPECT_FALSE(model()->GetTab(plain)->manual_name);
  EXPECT_NE(alpha, gamma);
  size_t today = 0;
  for (const auto& [id, tab] : model()->tabs()) {
    if (tab.section == Section::kToday) {
      ++today;
    }
  }
  EXPECT_EQ(today, TabCount());
}

}  // namespace aerie_tab_model
