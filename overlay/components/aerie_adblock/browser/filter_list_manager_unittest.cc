// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_adblock/browser/filter_list_manager.h"

#include <memory>
#include <string>
#include <vector>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/memory/raw_ref.h"
#include "base/run_loop.h"
#include "base/strings/string_number_conversions.h"
#include "base/task/thread_pool.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "components/aerie_adblock/browser/adblock_engine.h"
#include "components/aerie_adblock/browser/filter_list_storage.h"
#include "components/aerie_adblock/browser/session_key.h"
#include "net/http/http_status_code.h"
#include "services/network/public/cpp/weak_wrapper_shared_url_loader_factory.h"
#include "services/network/test/test_url_loader_factory.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace aerie_adblock {
namespace {

constexpr char kCustomUrl[] = "https://lists.example/custom.txt";

std::string ShippedEngine() {
  AdblockEngine engine;
  engine.LoadListForTesting("||shipped.example^\n");
  std::vector<uint8_t> bytes = engine.SerializeForTesting();
  return std::string(bytes.begin(), bytes.end());
}

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

class FilterListManagerTest : public testing::Test {
 protected:
  FilterListManagerTest()
      : service_(base::ThreadPool::CreateSequencedTaskRunner({base::MayBlock()})) {
    static const std::string kEngine = ShippedEngine();
    service_.Start(kEngine);
  }

  void SetUp() override { ASSERT_TRUE(dir_.CreateUniqueTempDir()); }

  std::unique_ptr<FilterListManager> MakeManager() {
    auto manager = std::make_unique<FilterListManager>(
        dir_.GetPath(), service_.GetWeakPtr(),
        base::MakeRefCounted<network::WeakWrapperSharedURLLoaderFactory>(&network_),
        base::BindRepeating([](std::string_view id) {
          return std::string(id == kEasyListId ? "||builtin-one.example^\n"
                                               : "||builtin-two.example^\n");
        }),
        /*allow_loopback=*/false);
    manager->Init();
    WaitUntilIdle(*manager);
    return manager;
  }

  void WaitUntilIdle(FilterListManager& manager) {
    IdleWaiter waiter(manager);
    waiter.Wait();
  }

  bool Blocks(const char* host) {
    ClassifiedRequest request{std::string("https://") + host + "/a.js", host,
                              "news.test", RequestKind::kScript, true};
    bool answer = false;
    base::RunLoop loop;
    service_.ShouldBlock(request, base::BindLambdaForTesting([&](bool block) {
                           answer = block;
                           loop.Quit();
                         }));
    loop.Run();
    return answer;
  }

  base::test::TaskEnvironment task_environment_;
  base::ScopedTempDir dir_;
  FilterEngineService service_;
  network::TestURLLoaderFactory network_;
};

TEST_F(FilterListManagerTest, AFreshStartKeepsTheShippedEngineAndFetchesNothing) {
  auto manager = MakeManager();
  EXPECT_EQ(2u, manager->state().lists().size());
  EXPECT_FALSE(manager->busy());
  EXPECT_TRUE(Blocks("shipped.example"));
  EXPECT_EQ(0, network_.NumPending());
  EXPECT_EQ(0u, network_.total_requests());
}

TEST_F(FilterListManagerTest, ACustomListTakesEffectWithoutARestart) {
  network_.AddResponse(kCustomUrl, "! Title: Custom\n||custom.example^\n");
  auto manager = MakeManager();
  EXPECT_FALSE(Blocks("custom.example"));

  std::string error;
  ASSERT_TRUE(manager->AddList(GURL(kCustomUrl), &error)) << error;
  WaitUntilIdle(*manager);

  EXPECT_TRUE(Blocks("custom.example"));
  // The built-in lists are compiled from the shipped text now, so the shipped
  // engine's own rules are replaced by the same lists.
  EXPECT_TRUE(Blocks("builtin-one.example"));
  const FilterListEntry& entry = manager->state().lists().back();
  EXPECT_EQ("Custom", entry.title);
  EXPECT_EQ(1, entry.rules);
  EXPECT_EQ("", entry.error);
  EXPECT_FALSE(entry.updated.is_null());
  // Only the list's own address was asked for.
  EXPECT_EQ(1u, network_.total_requests());
}

TEST_F(FilterListManagerTest, ACustomListSurvivesARestart) {
  network_.AddResponse(kCustomUrl, "||custom.example^\n");
  {
    auto manager = MakeManager();
    std::string error;
    ASSERT_TRUE(manager->AddList(GURL(kCustomUrl), &error));
    WaitUntilIdle(*manager);
  }
  // A new manager and engine over the same files, with no network at all.
  FilterEngineService second(base::ThreadPool::CreateSequencedTaskRunner({base::MayBlock()}));
  static const std::string kEngine = ShippedEngine();
  second.Start(kEngine);
  network::TestURLLoaderFactory none;
  FilterListManager manager(
      dir_.GetPath(), second.GetWeakPtr(),
      base::MakeRefCounted<network::WeakWrapperSharedURLLoaderFactory>(&none),
      base::BindRepeating([](std::string_view) { return std::string("||b.example^\n"); }),
      false);
  manager.Init();
  WaitUntilIdle(manager);
  ClassifiedRequest request{"https://custom.example/a.js", "custom.example", "news.test",
                            RequestKind::kScript, true};
  bool blocked = false;
  base::RunLoop loop;
  second.ShouldBlock(request, base::BindLambdaForTesting([&](bool b) {
                       blocked = b;
                       loop.Quit();
                     }));
  loop.Run();
  EXPECT_TRUE(blocked);
  EXPECT_EQ(0u, none.total_requests());
}

TEST_F(FilterListManagerTest, AnInsecureOrDuplicateAddressIsRefusedWithoutAnyRequest) {
  auto manager = MakeManager();
  std::string error;
  EXPECT_FALSE(manager->AddList(GURL("http://lists.example/a.txt"), &error));
  EXPECT_FALSE(error.empty());
  EXPECT_FALSE(manager->AddList(GURL("https://user:pw@lists.example/a.txt"), &error));
  EXPECT_FALSE(manager->AddList(GURL(kEasyListUrl), &error));  // Already there.
  EXPECT_EQ(2u, manager->state().lists().size());
  EXPECT_EQ(0u, network_.total_requests());
}

TEST_F(FilterListManagerTest, ABadDownloadIsShownAndChangesNothing) {
  network_.AddResponse("https://lists.example/page.txt", "<html>Not a list</html>");
  network_.AddResponse("https://lists.example/gone.txt", "nope", net::HTTP_NOT_FOUND);
  auto manager = MakeManager();
  std::string error;
  ASSERT_TRUE(manager->AddList(GURL("https://lists.example/page.txt"), &error));
  ASSERT_TRUE(manager->AddList(GURL("https://lists.example/gone.txt"), &error));
  WaitUntilIdle(*manager);

  const auto& lists = manager->state().lists();
  EXPECT_EQ(ListFailureMessage(ListFailure::kLooksLikeHtml), lists[2].error);
  EXPECT_EQ(ListFailureMessage(ListFailure::kDownload), lists[3].error);
  EXPECT_TRUE(lists[2].updated.is_null());
  EXPECT_TRUE(Blocks("shipped.example"));  // The shipped engine still runs.
  EXPECT_FALSE(base::PathExists(filter_list_storage::ListPath(dir_.GetPath(), lists[2].id)));
}

TEST_F(FilterListManagerTest, AFailedUpdateKeepsTheWorkingCopy) {
  network_.AddResponse(kCustomUrl, "||custom.example^\n");
  auto manager = MakeManager();
  std::string error;
  ASSERT_TRUE(manager->AddList(GURL(kCustomUrl), &error));
  WaitUntilIdle(*manager);
  ASSERT_TRUE(Blocks("custom.example"));
  std::string id = manager->state().lists().back().id;
  base::Time first_update = manager->state().lists().back().updated;

  network_.ClearResponses();
  network_.AddResponse(kCustomUrl, "<!DOCTYPE html><title>oops</title>");
  manager->UpdateList(id);
  WaitUntilIdle(*manager);

  EXPECT_TRUE(Blocks("custom.example"));
  const FilterListEntry* entry = manager->state().Find(id);
  EXPECT_EQ(ListFailureMessage(ListFailure::kLooksLikeHtml), entry->error);
  EXPECT_EQ(first_update, entry->updated);
  EXPECT_EQ(1, entry->rules);
  std::string saved;
  ASSERT_TRUE(base::ReadFileToString(filter_list_storage::ListPath(dir_.GetPath(), id), &saved));
  EXPECT_EQ("||custom.example^\n", saved);
}

TEST_F(FilterListManagerTest, ARemovedListStopsBlocking) {
  network_.AddResponse(kCustomUrl, "||custom.example^\n");
  auto manager = MakeManager();
  std::string error;
  ASSERT_TRUE(manager->AddList(GURL(kCustomUrl), &error));
  WaitUntilIdle(*manager);
  std::string id = manager->state().lists().back().id;

  manager->RemoveList("easylist");  // Built-in lists stay.
  EXPECT_EQ(3u, manager->state().lists().size());
  manager->RemoveList(id);
  WaitUntilIdle(*manager);
  EXPECT_FALSE(Blocks("custom.example"));
  EXPECT_EQ(2u, manager->state().lists().size());
  EXPECT_FALSE(base::PathExists(filter_list_storage::ListPath(dir_.GetPath(), id)));
}

TEST_F(FilterListManagerTest, ASiteExceptionWorksAtOnceAndIsSaved) {
  auto manager = MakeManager();
  const SessionKey regular = SessionKey::Regular();
  ASSERT_EQ(ExceptionResult::kOk,
            manager->SetExceptionForTesting("News.Test", true, regular));
  EXPECT_TRUE(manager->HasException("news.test", regular));
  EXPECT_TRUE(manager->HasException("www.news.test", regular));
  EXPECT_TRUE(service_.exceptions().count("news.test"));
  EXPECT_EQ(ExceptionResult::kInvalidHost,
            manager->SetExceptionForTesting("not a host", true, regular));

  manager.reset();
  base::RunLoop().RunUntilIdle();
  task_environment_.RunUntilIdle();
  EXPECT_EQ(std::set<std::string>{"news.test"},
            filter_list_storage::LoadState(dir_.GetPath()).exceptions());

  FilterEngineService second(base::ThreadPool::CreateSequencedTaskRunner({base::MayBlock()}));
  FilterListManager again(
      dir_.GetPath(), second.GetWeakPtr(), nullptr,
      base::BindRepeating([](std::string_view) { return std::string(); }), false);
  again.Init();
  WaitUntilIdle(again);
  EXPECT_TRUE(second.exceptions().count("news.test"));
}

TEST_F(FilterListManagerTest, AnIncognitoExceptionIsMemoryOnlyAndScopedToItsSession) {
  auto manager = MakeManager();
  const SessionKey regular = SessionKey::Regular();
  int id = 0, other_id = 0;
  const SessionKey incognito = SessionKey::OffTheRecord(&id);
  const SessionKey other_incognito = SessionKey::OffTheRecord(&other_id);
  ASSERT_EQ(ExceptionResult::kOk,
            manager->SetExceptionForTesting("News.Test", true, incognito));
  EXPECT_TRUE(manager->HasException("news.test", incognito));
  EXPECT_TRUE(manager->HasException("www.news.test", incognito));
  // Not in the regular profile, not in another incognito session.
  EXPECT_FALSE(manager->HasException("news.test", regular));
  EXPECT_FALSE(manager->HasException("news.test", other_incognito));
  EXPECT_FALSE(manager->HasSavedException("news.test"));
  EXPECT_TRUE(manager->state().exceptions().empty());
  EXPECT_TRUE(service_.exceptions().empty());
  EXPECT_EQ(std::set<std::string>{"news.test"}, manager->SessionExceptions(incognito));
  EXPECT_EQ(ExceptionResult::kInvalidHost,
            manager->SetExceptionForTesting("not a host", true, incognito));

  // Nothing reaches the disk, even after the saves have run.
  manager->SetExceptionForTesting("regular.test", true,
                                  regular);  // Forces a save of the state.
  manager.reset();
  base::RunLoop().RunUntilIdle();
  task_environment_.RunUntilIdle();
  EXPECT_EQ(std::set<std::string>{"regular.test"},
            filter_list_storage::LoadState(dir_.GetPath()).exceptions());
  std::string file;
  ASSERT_TRUE(base::ReadFileToString(
      dir_.GetPath().AppendASCII("state.json"), &file));
  EXPECT_EQ(std::string::npos, file.find("news.test"));
}

TEST_F(FilterListManagerTest, AFullExceptionTableIsNotAnInvalidHost) {
  auto manager = MakeManager();
  int id = 0;
  const SessionKey regular = SessionKey::Regular();
  const SessionKey incognito = SessionKey::OffTheRecord(&id);
  for (size_t i = 0; i < kMaxExceptions; ++i) {
    const std::string host = "host" + base::NumberToString(i) + ".test";
    ASSERT_EQ(ExceptionResult::kOk,
              manager->SetExceptionForTesting(host, true, regular));
    ASSERT_EQ(ExceptionResult::kOk,
              manager->SetExceptionForTesting(host + "x", true, incognito));
  }
  EXPECT_EQ(ExceptionResult::kTableFull,
            manager->SetExceptionForTesting("one-more.test", true, regular));
  EXPECT_EQ(ExceptionResult::kTableFull,
            manager->SetExceptionForTesting("one-more.test", true, incognito));
  // A bad name is still a bad name, and a host already in the table is fine.
  EXPECT_EQ(ExceptionResult::kInvalidHost,
            manager->SetExceptionForTesting("not a host", true, regular));
  EXPECT_EQ(ExceptionResult::kInvalidHost,
            manager->SetExceptionForTesting("not a host", true, incognito));
  EXPECT_EQ(ExceptionResult::kOk,
            manager->SetExceptionForTesting("host0.test", true, regular));
  EXPECT_EQ(ExceptionResult::kOk,
            manager->SetExceptionForTesting("host0.testx", true, incognito));
}

TEST_F(FilterListManagerTest, ClearingASessionDropsItsExceptions) {
  auto manager = MakeManager();
  int id = 0;
  const SessionKey incognito = SessionKey::OffTheRecord(&id);
  manager->SetExceptionForTesting("news.test", true, incognito);
  manager->ClearSessionExceptions(incognito);
  EXPECT_FALSE(manager->HasException("news.test", incognito));
  EXPECT_TRUE(manager->SessionExceptions(incognito).empty());
  // The engine side is cleared too: ads are blocked again for that session.
  EXPECT_FALSE(service_.IsExcepted("news.test", incognito));
}

TEST_F(FilterListManagerTest, ASavedExceptionAlsoHoldsInIncognito) {
  auto manager = MakeManager();
  int id = 0;
  const SessionKey incognito = SessionKey::OffTheRecord(&id);
  manager->SetExceptionForTesting("news.test", true, SessionKey::Regular());
  EXPECT_TRUE(manager->HasException("news.test", incognito));
  EXPECT_TRUE(manager->HasSavedException("news.test"));
}

TEST_F(FilterListManagerTest, IncognitoCannotRemoveASavedException) {
  auto manager = MakeManager();
  int id = 0;
  const SessionKey incognito = SessionKey::OffTheRecord(&id);
  ASSERT_EQ(ExceptionResult::kOk,
            manager->SetExceptionForTesting("news.test", true,
                                            SessionKey::Regular()));
  EXPECT_TRUE(manager->IsExceptionLocked("news.test", incognito));
  EXPECT_TRUE(manager->IsExceptionLocked("www.news.test", incognito));
  EXPECT_FALSE(manager->IsExceptionLocked("news.test", SessionKey::Regular()));
  EXPECT_FALSE(manager->IsExceptionLocked("other.test", incognito));

  EXPECT_EQ(ExceptionResult::kSavedInRegularProfile,
            manager->SetExceptionForTesting("news.test", false, incognito));
  EXPECT_TRUE(manager->HasSavedException("news.test"));
  EXPECT_TRUE(service_.exceptions().count("news.test"));
  // The regular profile can still remove it.
  EXPECT_EQ(ExceptionResult::kOk,
            manager->SetExceptionForTesting("news.test", false,
                                            SessionKey::Regular()));
  EXPECT_FALSE(manager->HasSavedException("news.test"));
}

TEST_F(FilterListManagerTest, AHostThatIsSavedAndSetInIncognitoIsListedOnce) {
  auto manager = MakeManager();
  int id = 0;
  const SessionKey incognito = SessionKey::OffTheRecord(&id);
  manager->SetExceptionForTesting("news.test", true, SessionKey::Regular());
  manager->SetExceptionForTesting("news.test", true, incognito);
  manager->SetExceptionForTesting("only-here.test", true, incognito);
  EXPECT_EQ((std::vector<std::string>{"news.test", "only-here.test"}),
            manager->ExceptionsFor(incognito));
  // The regular page lists only what is saved.
  EXPECT_EQ(std::vector<std::string>{"news.test"},
            manager->ExceptionsFor(SessionKey::Regular()));
}

TEST_F(FilterListManagerTest, TheScheduledUpdateWaitsADayThenAsksOnlyThePublishers) {
  network_.AddResponse(kEasyListUrl, "! Title: EasyList\n||new-one.example^\n");
  network_.AddResponse(kEasyPrivacyUrl, "||new-two.example^\n");
  network_.AddResponse(kCustomUrl, "||custom.example^\n");
  auto manager = MakeManager();
  std::string error;
  ASSERT_TRUE(manager->AddList(GURL(kCustomUrl), &error));
  WaitUntilIdle(*manager);
  ASSERT_EQ(1u, network_.total_requests());

  base::Time now = base::Time::Now();
  manager->MaybeRunScheduledUpdate(now);  // Install day: starts the clock.
  manager->MaybeRunScheduledUpdate(now + base::Hours(23));
  WaitUntilIdle(*manager);
  EXPECT_EQ(1u, network_.total_requests());

  manager->MaybeRunScheduledUpdate(now + base::Hours(25));
  WaitUntilIdle(*manager);
  EXPECT_EQ(4u, network_.total_requests());
  EXPECT_TRUE(Blocks("new-one.example"));
  EXPECT_TRUE(Blocks("custom.example"));
  EXPECT_FALSE(Blocks("builtin-one.example"));  // Replaced by the new copy.

  // Not again until another day has passed.
  manager->MaybeRunScheduledUpdate(now + base::Hours(26));
  WaitUntilIdle(*manager);
  EXPECT_EQ(4u, network_.total_requests());
}

TEST_F(FilterListManagerTest, TurningAutomaticUpdatesOffStopsTheSchedule) {
  auto manager = MakeManager();
  manager->SetAutoUpdate(false);
  base::Time now = base::Time::Now();
  manager->MaybeRunScheduledUpdate(now);
  manager->MaybeRunScheduledUpdate(now + base::Days(30));
  WaitUntilIdle(*manager);
  EXPECT_EQ(0u, network_.total_requests());
}

TEST_F(FilterListManagerTest, ACorruptSavedCopyFallsBackToTheShippedList) {
  network_.AddResponse(kEasyListUrl, "||updated.example^\n");
  {
    auto manager = MakeManager();
    manager->UpdateList("easylist");
    WaitUntilIdle(*manager);
  }
  ASSERT_TRUE(base::WriteFile(filter_list_storage::ListPath(dir_.GetPath(), "easylist"),
                              "<html>damaged</html>"));
  FilterEngineService second(base::ThreadPool::CreateSequencedTaskRunner({base::MayBlock()}));
  static const std::string kEngine = ShippedEngine();
  second.Start(kEngine);
  FilterListManager manager(
      dir_.GetPath(), second.GetWeakPtr(), nullptr,
      base::BindRepeating([](std::string_view id) { return std::string("||builtin-x.example^\n"); }),
      false);
  manager.Init();
  WaitUntilIdle(manager);
  // The bad copy is dropped and the shipped list is used in its place.
  const FilterListEntry* entry = manager.state().Find("easylist");
  EXPECT_TRUE(entry->updated.is_null());
  EXPECT_EQ(ListFailureMessage(ListFailure::kLooksLikeHtml), entry->error);
}

}  // namespace
}  // namespace aerie_adblock
