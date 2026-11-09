// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

// The target of a Guard panel (issue #100): captured from the browser's own
// state, and good only while the tab lives and shows the captured host.

#include "components/aerie_adblock/browser/guard_target.h"

#include <memory>
#include <string>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/memory/raw_ref.h"
#include "base/run_loop.h"
#include "base/task/thread_pool.h"
#include "components/aerie_adblock/browser/filter_engine_service.h"
#include "components/aerie_adblock/browser/filter_list_manager.h"
#include "components/aerie_adblock/browser/page_block_counter.h"
#include "components/aerie_adblock/browser/session_key.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/navigation_simulator.h"
#include "content/public/test/test_browser_context.h"
#include "content/public/test/test_renderer_host.h"
#include "content/public/test/web_contents_tester.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace aerie_adblock {
namespace {

// Quits a RunLoop once the manager is idle.
class IdleWaiter : public FilterListManager::Observer {
 public:
  explicit IdleWaiter(FilterListManager& manager) : manager_(manager) {
    manager_->AddObserver(this);
  }
  ~IdleWaiter() override { manager_->RemoveObserver(this); }
  void Wait() {
    if (manager_->busy()) {
      loop_.Run();
    }
  }
  void OnFilterListsChanged() override {
    if (!manager_->busy()) {
      loop_.Quit();
    }
  }

 private:
  raw_ref<FilterListManager> manager_;
  base::RunLoop loop_;
};

class GuardTargetTest : public content::RenderViewHostTestHarness {
 protected:
  GuardTargetTest()
      : service_(
            base::ThreadPool::CreateSequencedTaskRunner({base::MayBlock()})) {}

  void SetUp() override {
    content::RenderViewHostTestHarness::SetUp();
    ASSERT_TRUE(dir_.CreateUniqueTempDir());
    manager_ = std::make_unique<FilterListManager>(
        dir_.GetPath(), service_.GetWeakPtr(), nullptr,
        base::BindRepeating([](std::string_view) { return std::string(); }),
        /*allow_loopback=*/false);
    manager_->SetAsProcessInstance();
    manager_->Init();
    // Exceptions set before the state file has loaded would be dropped.
    IdleWaiter waiter(*manager_);
    waiter.Wait();
  }

  void TearDown() override {
    manager_.reset();
    content::RenderViewHostTestHarness::TearDown();
  }

  base::ScopedTempDir dir_;
  FilterEngineService service_;
  std::unique_ptr<FilterListManager> manager_;
};

TEST_F(GuardTargetTest, CapturesTheHostOfAnHttpsPage) {
  NavigateAndCommit(GURL("https://news.test/story"));
  const GuardTarget target = GuardTarget::Capture(web_contents());
  EXPECT_FALSE(target.empty());
  EXPECT_EQ("news.test", target.host());
  ASSERT_TRUE(target.Resolve());
  EXPECT_TRUE(target.Resolve()->visible);
  EXPECT_FALSE(target.Resolve()->blocking_off);
}

TEST_F(GuardTargetTest, AnythingButHttpOrHttpsIsEmptyAndNeverResolves) {
  EXPECT_TRUE(GuardTarget::Capture(nullptr).empty());
  EXPECT_FALSE(GuardTarget().Resolve());
  for (const char* url : {"chrome://settings/", "file:///tmp/a.html",
                          "about:blank"}) {
    NavigateAndCommit(GURL(url));
    const GuardTarget target = GuardTarget::Capture(web_contents());
    EXPECT_TRUE(target.empty()) << url;
    EXPECT_FALSE(target.Resolve()) << url;
    EXPECT_EQ(GuardTarget::Toggle::kStale, target.SetBlockingOn(false)) << url;
  }
}

TEST_F(GuardTargetTest, ANavigationToAnotherSiteMakesTheTargetStale) {
  NavigateAndCommit(GURL("https://news.test/"));
  const GuardTarget target = GuardTarget::Capture(web_contents());
  NavigateAndCommit(GURL("https://other.test/"));
  EXPECT_FALSE(target.Resolve());
  EXPECT_EQ(GuardTarget::Toggle::kStale, target.SetBlockingOn(false));
  // Nothing changed, for the old site or the new one.
  EXPECT_FALSE(manager_->HasSavedException("news.test"));
  EXPECT_FALSE(manager_->HasSavedException("other.test"));
}

TEST_F(GuardTargetTest, ANavigationOnTheSameHostKeepsTheTarget) {
  NavigateAndCommit(GURL("https://news.test/a"));
  const GuardTarget target = GuardTarget::Capture(web_contents());
  NavigateAndCommit(GURL("https://news.test/b"));
  EXPECT_TRUE(target.Resolve());
}

TEST_F(GuardTargetTest, AClosedTabMakesTheTargetStale) {
  content::TestBrowserContext context;
  std::unique_ptr<content::WebContents> tab =
      content::WebContentsTester::CreateTestWebContents(&context, nullptr);
  content::NavigationSimulator::NavigateAndCommitFromBrowser(
      tab.get(), GURL("https://news.test/"));
  const GuardTarget target = GuardTarget::Capture(tab.get());
  ASSERT_TRUE(target.Resolve());
  tab.reset();
  EXPECT_FALSE(target.Resolve());
  EXPECT_EQ(nullptr, target.tab());
  EXPECT_EQ(GuardTarget::Toggle::kStale, target.SetBlockingOn(false));
  EXPECT_FALSE(manager_->HasSavedException("news.test"));
}

TEST_F(GuardTargetTest, TurnsGuardOffAndOnForTheCapturedSite) {
  NavigateAndCommit(GURL("https://news.test/"));
  const GuardTarget target = GuardTarget::Capture(web_contents());
  EXPECT_EQ(GuardTarget::Toggle::kApplied, target.SetBlockingOn(false));
  EXPECT_TRUE(manager_->HasSavedException("news.test"));
  EXPECT_TRUE(target.Resolve()->blocking_off);
  EXPECT_EQ(GuardTarget::Toggle::kApplied, target.SetBlockingOn(true));
  EXPECT_FALSE(manager_->HasSavedException("news.test"));
  EXPECT_FALSE(target.Resolve()->blocking_off);
}

TEST_F(GuardTargetTest, AnIncognitoToggleStaysInMemory) {
  // A saved exception makes the state file exist, so "unchanged" means bytes.
  ASSERT_EQ(ExceptionResult::kOk,
            manager_->SetException("saved.test", true, browser_context()));
  task_environment()->RunUntilIdle();
  const base::FilePath state_file = dir_.GetPath().AppendASCII("state.json");
  std::string before;
  ASSERT_TRUE(base::ReadFileToString(state_file, &before));

  content::TestBrowserContext incognito;
  incognito.set_is_off_the_record(true);
  std::unique_ptr<content::WebContents> tab =
      content::WebContentsTester::CreateTestWebContents(&incognito, nullptr);
  content::NavigationSimulator::NavigateAndCommitFromBrowser(
      tab.get(), GURL("https://news.test/"));
  const GuardTarget target = GuardTarget::Capture(tab.get());
  ASSERT_EQ(GuardTarget::Toggle::kApplied, target.SetBlockingOn(false));
  EXPECT_TRUE(manager_->HasException("news.test", SessionKeyFor(&incognito)));
  // Nothing reached the regular profile or the state file.
  EXPECT_FALSE(manager_->HasSavedException("news.test"));
  EXPECT_FALSE(manager_->HasException("news.test", SessionKey::Regular()));
  task_environment()->RunUntilIdle();
  std::string after;
  ASSERT_TRUE(base::ReadFileToString(state_file, &after));
  EXPECT_EQ(before, after);
  EXPECT_EQ(std::string::npos, after.find("news.test"));
  tab.reset();
}

TEST_F(GuardTargetTest, AnIncognitoTabCannotUndoASavedException) {
  ASSERT_EQ(ExceptionResult::kOk,
            manager_->SetException("news.test", true, browser_context()));
  content::TestBrowserContext incognito;
  incognito.set_is_off_the_record(true);
  std::unique_ptr<content::WebContents> tab =
      content::WebContentsTester::CreateTestWebContents(&incognito, nullptr);
  content::NavigationSimulator::NavigateAndCommitFromBrowser(
      tab.get(), GURL("https://news.test/"));
  const GuardTarget target = GuardTarget::Capture(tab.get());
  ASSERT_TRUE(target.Resolve());
  EXPECT_TRUE(target.Resolve()->locked);
  EXPECT_EQ(GuardTarget::Toggle::kRefused, target.SetBlockingOn(true));
  EXPECT_TRUE(manager_->HasSavedException("news.test"));
  tab.reset();
}

}  // namespace
}  // namespace aerie_adblock
