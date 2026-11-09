// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

// The manager's off-the-record rules against real BrowserContexts: the manager
// watches the context itself, so a UI caller only passes the context.

#include <memory>
#include <set>
#include <string>

#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/memory/raw_ref.h"
#include "base/run_loop.h"
#include "base/task/thread_pool.h"
#include "components/aerie_adblock/browser/filter_engine_service.h"
#include "components/aerie_adblock/browser/filter_list_manager.h"
#include "components/aerie_adblock/browser/session_key.h"
#include "content/public/test/browser_task_environment.h"
#include "content/public/test/test_browser_context.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "testing/gtest/include/gtest/gtest.h"

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

class FilterListManagerSessionTest : public testing::Test {
 protected:
  FilterListManagerSessionTest()
      : service_(base::ThreadPool::CreateSequencedTaskRunner({base::MayBlock()})) {}

  void SetUp() override {
    ASSERT_TRUE(dir_.CreateUniqueTempDir());
    manager_ = std::make_unique<FilterListManager>(
        dir_.GetPath(), service_.GetWeakPtr(), nullptr,
        base::BindRepeating([](std::string_view) { return std::string(); }),
        /*allow_loopback=*/false);
    manager_->SetAsProcessInstance();  // The session watch finds the manager there.
    manager_->Init();
    // Wait for the state file to load: exceptions set before it would be
    // dropped by the load.
    IdleWaiter waiter(*manager_);
    waiter.Wait();
  }

  void TearDown() override {
    manager_.reset();
    task_environment_.RunUntilIdle();
  }

  content::BrowserTaskEnvironment task_environment_;
  base::ScopedTempDir dir_;
  FilterEngineService service_;
  std::unique_ptr<FilterListManager> manager_;
};

TEST_F(FilterListManagerSessionTest, AnExceptionSetFromAContextGoesWhenTheContextDoes) {
  auto incognito = std::make_unique<content::TestBrowserContext>();
  incognito->set_is_off_the_record(true);
  const SessionKey key = SessionKeyFor(incognito.get());
  ASSERT_TRUE(key.is_off_the_record());

  // No WatchSession() call by the caller: the manager does it.
  ASSERT_EQ(ExceptionResult::kOk,
            manager_->SetException("news.test", true, incognito.get()));
  EXPECT_TRUE(manager_->HasException("news.test", key));
  EXPECT_FALSE(manager_->HasException("news.test", SessionKey::Regular()));

  incognito.reset();  // The window closed.
  EXPECT_FALSE(manager_->HasException("news.test", key));
  EXPECT_FALSE(service_.IsExcepted("news.test", key));
}

TEST_F(FilterListManagerSessionTest, ARegularContextSavesTheException) {
  content::TestBrowserContext regular;
  ASSERT_EQ(ExceptionResult::kOk,
            manager_->SetException("news.test", true, &regular));
  EXPECT_TRUE(manager_->HasSavedException("news.test"));
}

TEST_F(FilterListManagerSessionTest, AnOffTheRecordContextCannotRemoveASavedException) {
  content::TestBrowserContext regular;
  content::TestBrowserContext incognito;
  incognito.set_is_off_the_record(true);
  ASSERT_EQ(ExceptionResult::kOk,
            manager_->SetException("news.test", true, &regular));
  EXPECT_EQ(ExceptionResult::kSavedInRegularProfile,
            manager_->SetException("news.test", false, &incognito));
  EXPECT_TRUE(manager_->HasSavedException("news.test"));
}

TEST_F(FilterListManagerSessionTest, ACosmeticChoiceSetFromAContextGoesWhenTheContextDoes) {
  auto incognito = std::make_unique<content::TestBrowserContext>();
  incognito->set_is_off_the_record(true);
  const SessionKey key = SessionKeyFor(incognito.get());
  ASSERT_EQ(ExceptionResult::kOk,
            manager_->SetCosmeticHidingOff("news.test", true, incognito.get()));
  EXPECT_TRUE(manager_->IsCosmeticHidingOff("news.test", key));
  EXPECT_FALSE(manager_->IsCosmeticHidingOff("news.test", SessionKey::Regular()));
  incognito.reset();  // The window closed.
  EXPECT_FALSE(manager_->IsCosmeticHidingOff("news.test", key));
}

}  // namespace
}  // namespace aerie_adblock
