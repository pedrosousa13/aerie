// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

// Browser test for the Tab Model's binding to the tab strip (issue #176).
//
// This file is not in a BUILD.gn yet. It needs the Aerie browser-test target
// that issue #175 (slice O) adds; whoever merges the second of the two adds
// "tab_model_browsertest.cc" to that target's sources, with deps on
// ":aerie_tab_model", "//components/aerie_tab_model" and the
// pinned_tab_codec target. Until then `gn check` and the build do not see it.

#include <string>

#include "base/run_loop.h"
#include "chrome/browser/aerie_tab_model/tab_model_service.h"
#include "chrome/browser/aerie_tab_model/tab_model_service_factory.h"
#include "chrome/browser/aerie_tab_model/tab_strip_adapter.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/pinned_tab_codec.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/aerie_tab_model/tab_model.h"
#include "components/aerie_tab_model/tab_model_observer.h"
#include "content/public/test/browser_test.h"
#include "net/test/embedded_test_server/embedded_test_server.h"

namespace aerie_tab_model {

namespace {

class LoadedWaiter : public TabModelObserver {
 public:
  explicit LoadedWaiter(TabModel* model) : model_(model) { model_->AddObserver(this); }
  ~LoadedWaiter() override { model_->RemoveObserver(this); }
  void Wait() { run_loop_.Run(); }
  void OnModelLoaded() override { run_loop_.Quit(); }

 private:
  raw_ptr<TabModel> model_;
  base::RunLoop run_loop_;
};

size_t CountToday(const TabModel& model) {
  size_t count = 0;
  for (const auto& [id, tab] : model.tabs()) {
    if (tab.section == Section::kToday) {
      ++count;
    }
  }
  return count;
}

}  // namespace

class TabModelBrowserTest : public InProcessBrowserTest {
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

  TabModelService* service_ = nullptr;
};

IN_PROC_BROWSER_TEST_F(TabModelBrowserTest, OpenedNavigatedMovedAndClosedTabsFollow) {
  TabModel* model = service_->model();
  TabStripModel* strip = browser()->tab_strip_model();
  // The window's first tab is a Today record.
  ASSERT_EQ(strip->count(), 1);
  EXPECT_EQ(CountToday(*model), 1u);

  // Opened.
  ASSERT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
      browser(), embedded_test_server()->GetURL("/title1.html"),
      WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
  ASSERT_EQ(strip->count(), 2);
  EXPECT_EQ(CountToday(*model), 2u);

  // Navigated: the record stays.
  std::string id = service_->adapter()->RecordIdForTab(strip->GetTabAtIndex(1));
  ASSERT_FALSE(id.empty());
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title2.html")));
  EXPECT_EQ(service_->adapter()->RecordIdForTab(strip->GetTabAtIndex(1)), id);
  EXPECT_EQ(CountToday(*model), 2u);

  // Moved: the position follows.
  strip->MoveWebContentsAt(1, 0, /*select_after_move=*/false);
  ASSERT_TRUE(model->GetTab(id));
  EXPECT_EQ(model->GetTab(id)->position, 0);
  EXPECT_EQ(model->GetTab(id)->section, Section::kToday);

  // Closed: the record goes.
  strip->CloseWebContentsAt(0, TabCloseTypes::CLOSE_NONE);
  EXPECT_EQ(model->GetTab(id), nullptr);
  EXPECT_EQ(CountToday(*model), 1u);
}

IN_PROC_BROWSER_TEST_F(TabModelBrowserTest, TabsAreNotChromiumPinnedAndNothingIsSavedToRestore) {
  ASSERT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
      browser(), embedded_test_server()->GetURL("/title1.html"),
      WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
  TabStripModel* strip = browser()->tab_strip_model();
  for (int i = 0; i < strip->count(); ++i) {
    EXPECT_FALSE(strip->IsTabPinned(i));
  }
  // Chromium's PinnedTabService would restore from this; with no pinned bit it
  // has nothing, so Aerie's Pinned section cannot come back twice.
  PinnedTabCodec::WritePinnedTabs(browser()->GetProfile());
  EXPECT_TRUE(PinnedTabCodec::ReadPinnedTabs(browser()->GetProfile()).empty());
}

// An Incognito window gets its own Tab Model, separate from the regular
// profile's, and nothing of it is written to disk.
IN_PROC_BROWSER_TEST_F(TabModelBrowserTest, AnOffTheRecordProfileHasItsOwnModelAndWritesNothing) {
  BrowserWindowInterface* incognito = CreateIncognitoBrowser();
  Profile* otr = incognito->GetProfile();
  ASSERT_TRUE(otr->IsOffTheRecord());
  TabModelService* otr_service = TabModelServiceFactory::GetForProfile(otr);
  ASSERT_TRUE(otr_service);
  EXPECT_NE(otr_service, service_);
  if (!otr_service->loaded()) {
    LoadedWaiter waiter(otr_service->model());
    waiter.Wait();
  }
  // The Incognito window's first tab is a record in its model only.
  EXPECT_EQ(CountToday(*otr_service->model()), 1u);
  EXPECT_EQ(CountToday(*service_->model()), 1u);

  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      incognito, embedded_test_server()->GetURL("/title1.html")));
  EXPECT_EQ(CountToday(*service_->model()), 1u);
  // Nothing is read or written for it; the regular profile's model is on disk.
  EXPECT_FALSE(otr_service->persists_to_disk());
  EXPECT_TRUE(service_->persists_to_disk());
}

}  // namespace aerie_tab_model
