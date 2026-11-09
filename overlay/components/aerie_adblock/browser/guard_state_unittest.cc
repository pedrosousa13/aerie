// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_adblock/browser/guard_state.h"

#include <memory>
#include <string>

#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/memory/raw_ref.h"
#include "base/run_loop.h"
#include "base/task/thread_pool.h"
#include "base/test/task_environment.h"
#include "components/aerie_adblock/browser/filter_engine_service.h"
#include "components/aerie_adblock/browser/filter_list_manager.h"
#include "components/aerie_adblock/browser/session_key.h"
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

TEST(GuardBadgeLabelTest, IsEmptyForNothingBlocked) {
  EXPECT_EQ(u"", GuardBadgeLabel(0));
  EXPECT_EQ(u"", GuardBadgeLabel(-3));
}

TEST(GuardBadgeLabelTest, IsTheNumberUpTo99) {
  EXPECT_EQ(u"1", GuardBadgeLabel(1));
  EXPECT_EQ(u"42", GuardBadgeLabel(42));
  EXPECT_EQ(u"99", GuardBadgeLabel(99));
}

TEST(GuardBadgeLabelTest, IsEmptyWhenTheUserHidTheCount) {
  EXPECT_EQ(u"", GuardBadgeLabel(7, false));
  EXPECT_EQ(u"", GuardBadgeLabel(100, false));
  EXPECT_EQ(u"", GuardBadgeLabel(0, false));
}

TEST(GuardBadgeLabelTest, IsTheSameWhenTheCountIsShown) {
  for (int count : {-1, 0, 1, 42, 99, 100, 5000}) {
    EXPECT_EQ(GuardBadgeLabel(count), GuardBadgeLabel(count, true));
  }
}

TEST(GuardBadgeLabelTest, SaturatesAbove99) {
  EXPECT_EQ(u"99+", GuardBadgeLabel(100));
  EXPECT_EQ(u"99+", GuardBadgeLabel(123456));
}

class GuardStateTest : public testing::Test {
 protected:
  GuardStateTest()
      : service_(
            base::ThreadPool::CreateSequencedTaskRunner({base::MayBlock()})) {}

  void SetUp() override {
    ASSERT_TRUE(dir_.CreateUniqueTempDir());
    manager_ = std::make_unique<FilterListManager>(
        dir_.GetPath(), service_.GetWeakPtr(), nullptr,
        base::BindRepeating([](std::string_view) { return std::string(); }),
        /*allow_loopback=*/false);
    manager_->Init();
    // Exceptions set before the state file loads would be dropped by the load.
    IdleWaiter waiter(*manager_);
    waiter.Wait();
  }

  void TearDown() override {
    manager_.reset();
    task_environment_.RunUntilIdle();
  }

  GuardState StateFor(const char* url,
                      SessionKey session = SessionKey::Regular(),
                      int count = 0) {
    return ComputeGuardState(GURL(url), session, manager_.get(), count);
  }

  base::test::TaskEnvironment task_environment_;
  base::ScopedTempDir dir_;
  FilterEngineService service_;
  std::unique_ptr<FilterListManager> manager_;
};

TEST_F(GuardStateTest, ShowsOnHttpAndHttpsPages) {
  GuardState http = StateFor("http://news.test/story");
  EXPECT_TRUE(http.visible);
  EXPECT_EQ("news.test", http.host);
  GuardState https = StateFor("https://news.test:8443/story?q=1#top");
  EXPECT_TRUE(https.visible);
  EXPECT_EQ("news.test", https.host);
}

TEST_F(GuardStateTest, HidesOnPagesThatAreNotTheWeb) {
  for (const char* url :
       {"chrome://settings/", "chrome://newtab/", "aerie://settings/",
        "file:///home/user/page.html", "about:blank", "data:text/html,hi",
        "chrome-search://local-ntp/local-ntp.html", "ftp://files.test/", ""}) {
    SCOPED_TRACE(url);
    GuardState state = StateFor(url, SessionKey::Regular(), 5);
    EXPECT_FALSE(state.visible);
    EXPECT_FALSE(state.blocking_off);
    EXPECT_EQ("", state.host);
    EXPECT_EQ(0, state.count);
  }
}

TEST_F(GuardStateTest, CarriesTheBlockedCount) {
  EXPECT_EQ(0, StateFor("https://news.test/").count);
  EXPECT_EQ(7, StateFor("https://news.test/", SessionKey::Regular(), 7).count);
  EXPECT_EQ(0, StateFor("https://news.test/", SessionKey::Regular(), -1).count);
}

TEST_F(GuardStateTest,
       ASavedExceptionTurnsTheIconOffForTheSiteAndItsSubdomains) {
  ASSERT_EQ(ExceptionResult::kOk,
            manager_->SetExceptionForTesting("news.test", true,
                                             SessionKey::Regular()));
  GuardState state = StateFor("https://news.test/", SessionKey::Regular(), 4);
  EXPECT_TRUE(state.visible);
  EXPECT_TRUE(state.blocking_off);
  EXPECT_EQ(0, state.count);  // Nothing is blocked while it is off.
  EXPECT_TRUE(StateFor("https://www.news.test/").blocking_off);
  EXPECT_FALSE(StateFor("https://other.test/").blocking_off);
}

TEST_F(GuardStateTest, TurningTheExceptionOffRestoresTheOnState) {
  manager_->SetExceptionForTesting("news.test", true, SessionKey::Regular());
  manager_->SetExceptionForTesting("news.test", false, SessionKey::Regular());
  EXPECT_FALSE(StateFor("https://news.test/").blocking_off);
}

TEST_F(GuardStateTest, AnIncognitoTabShowsTheIconAndItsOwnException) {
  int incognito_profile = 0;
  int other_profile = 0;
  SessionKey incognito = SessionKey::OffTheRecord(&incognito_profile);
  SessionKey other = SessionKey::OffTheRecord(&other_profile);

  GuardState before = StateFor("https://news.test/", incognito, 3);
  EXPECT_TRUE(before.visible);
  EXPECT_FALSE(before.blocking_off);
  EXPECT_FALSE(before.locked);
  EXPECT_EQ(3, before.count);

  ASSERT_EQ(ExceptionResult::kOk,
            manager_->SetExceptionForTesting("news.test", true, incognito));
  EXPECT_TRUE(StateFor("https://news.test/", incognito).blocking_off);
  // It is the session's own: neither another incognito session nor the regular
  // profile sees it.
  EXPECT_FALSE(StateFor("https://news.test/", other).blocking_off);
  EXPECT_FALSE(
      StateFor("https://news.test/", SessionKey::Regular()).blocking_off);
}

TEST_F(GuardStateTest, ASavedExceptionIsLockedInIncognito) {
  int incognito_profile = 0;
  SessionKey incognito = SessionKey::OffTheRecord(&incognito_profile);
  manager_->SetExceptionForTesting("news.test", true, SessionKey::Regular());

  GuardState incognito_state = StateFor("https://news.test/", incognito);
  EXPECT_TRUE(incognito_state.blocking_off);
  EXPECT_TRUE(incognito_state.locked);
  // The regular window can undo it.
  EXPECT_FALSE(StateFor("https://news.test/").locked);
}

TEST(GuardStateWithoutManagerTest, BlockingIsOn) {
  GuardState state = ComputeGuardState(GURL("https://news.test/"),
                                       SessionKey::Regular(), nullptr, 2);
  EXPECT_TRUE(state.visible);
  EXPECT_FALSE(state.blocking_off);
  EXPECT_FALSE(state.locked);
  EXPECT_EQ(2, state.count);
}

TEST_F(GuardStateTest, CosmeticHidingOffShowsInTheStateAndNotAsBlockingOff) {
  EXPECT_FALSE(StateFor("https://news.test/").cosmetic_off);
  ASSERT_EQ(ExceptionResult::kOk,
            manager_->SetCosmeticHidingOffForTesting("news.test", true,
                                                     SessionKey::Regular()));
  GuardState state = StateFor("https://www.news.test/", SessionKey::Regular(), 3);
  EXPECT_TRUE(state.cosmetic_off);
  EXPECT_FALSE(state.cosmetic_locked);
  EXPECT_FALSE(state.blocking_off);
  EXPECT_EQ(3, state.count);  // Blocking is on: the count stays.
  EXPECT_FALSE(StateFor("https://other.test/").cosmetic_off);
}

TEST_F(GuardStateTest, ASavedCosmeticChoiceIsLockedInIncognito) {
  int id = 0;
  const SessionKey incognito = SessionKey::OffTheRecord(&id);
  ASSERT_EQ(ExceptionResult::kOk,
            manager_->SetCosmeticHidingOffForTesting("news.test", true,
                                                     SessionKey::Regular()));
  GuardState state = StateFor("https://news.test/", incognito);
  EXPECT_TRUE(state.cosmetic_off);
  EXPECT_TRUE(state.cosmetic_locked);
}

}  // namespace
}  // namespace aerie_adblock
