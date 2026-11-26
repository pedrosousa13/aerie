// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_adblock/browser/filter_list_manager.h"

#include <memory>
#include <optional>
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
#include "components/aerie_adblock/browser/catalogue.h"
#include "components/aerie_adblock/browser/filter_list_storage.h"
#include "components/aerie_adblock/browser/session_key.h"
#include "net/http/http_status_code.h"
#include "services/network/public/cpp/weak_wrapper_shared_url_loader_factory.h"
#include "services/network/public/cpp/resource_request.h"
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

  // A setter posts its Save() to the file sequence and returns. A test that
  // ends right after one leaves the write in flight, and dir_ is deleted under
  // it ("cannot save the filter list settings"). Run the pool dry first.
  void TearDown() override { task_environment_.RunUntilIdle(); }

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

  // busy() covers downloads, checks and rebuilds, not the saves and deletes
  // the manager posts to its file sequence. Without the second step a test
  // reads the disk, or ends and removes dir_, while one is still in flight.
  void WaitUntilIdle(FilterListManager& manager) {
    IdleWaiter waiter(manager);
    waiter.Wait();
    task_environment_.RunUntilIdle();  // Also runs the thread pool dry.
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

  // Mock time: a test moves the clock with AdvanceClock(). UpdateAll() stamps
  // the check with base::Time::Now(), so a test that passes a made-up "now" to
  // MaybeRunScheduledUpdate() disagrees with the stamp and the schedule fires
  // again on the next call.
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
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

TEST_F(FilterListManagerTest, ACustomListTitleIsCleanedAndBoundedByTheHeader) {
  // Control characters go, HTML stays text, and a title below the header is
  // not the list's name (issue #108).
  network_.AddResponse(kCustomUrl,
                       "! Title: <i>Mine</i>\x01 list\n||custom.example^\n"
                       "! Title: Not this one\n");
  auto manager = MakeManager();
  std::string error;
  ASSERT_TRUE(manager->AddList(GURL(kCustomUrl), &error)) << error;
  WaitUntilIdle(*manager);
  EXPECT_EQ("<i>Mine</i> list", manager->state().lists().back().title);
}

TEST_F(FilterListManagerTest, TheSourceOfACustomListIsReadByIdOnly) {
  const std::string text =
      "! Title: Mine\n<script>alert(1)</script>\n||custom.example^\n";
  network_.AddResponse(kCustomUrl, text);
  auto manager = MakeManager();
  std::string error;
  ASSERT_TRUE(manager->AddList(GURL(kCustomUrl), &error)) << error;
  WaitUntilIdle(*manager);
  const std::string id = manager->state().lists().back().id;

  std::optional<std::string> got;
  base::RunLoop loop;
  ASSERT_TRUE(manager->ReadListSource(
      id, base::BindLambdaForTesting([&](std::optional<std::string> source) {
        got = std::move(source);
        loop.Quit();
      })));
  loop.Run();
  ASSERT_TRUE(got.has_value());
  EXPECT_EQ(text, *got);

  // Unknown ids, addresses, built-in lists, the user's own rules and ids that
  // are not list ids are refused, and the callback never runs.
  for (const char* bad :
       {"", "nope", "easylist", "easyprivacy", "custom", kCustomUrl,
        "../state", "ABCDEF"}) {
    bool ran = false;
    EXPECT_FALSE(manager->ReadListSource(
        bad, base::BindLambdaForTesting(
                 [&](std::optional<std::string>) { ran = true; })))
        << bad;
    EXPECT_FALSE(ran) << bad;
  }
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

TEST_F(FilterListManagerTest, AnAddressOnTheUsersNetworkIsRefusedWithItsOwnReasonAndNoRequest) {
  auto manager = MakeManager();
  std::string error;
  for (const char* url : {"https://192.168.1.5/x.txt", "https://nas/list.txt",
                          "https://localhost/x.txt"}) {
    error.clear();
    EXPECT_FALSE(manager->AddList(GURL(url), &error)) << url;
    EXPECT_EQ(error, ListFailureMessage(ListFailure::kNonPublicUrl)) << url;
  }
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

TEST_F(FilterListManagerTest, CosmeticHidingOffIsSavedAndKeepsBlockingOn) {
  auto manager = MakeManager();
  const SessionKey regular = SessionKey::Regular();
  ASSERT_EQ(ExceptionResult::kOk,
            manager->SetCosmeticHidingOffForTesting("News.Test", true, regular));
  EXPECT_TRUE(manager->IsCosmeticHidingOff("news.test", regular));
  EXPECT_TRUE(manager->IsCosmeticHidingOff("www.news.test", regular));
  EXPECT_FALSE(manager->IsCosmeticHidingOff("other.test", regular));
  EXPECT_FALSE(manager->IsCosmeticHidingOff("notnews.test", regular));
  EXPECT_EQ(ExceptionResult::kInvalidHost,
            manager->SetCosmeticHidingOffForTesting("not a host", true, regular));
  // This is not a blocking exception.
  EXPECT_FALSE(manager->HasException("news.test", regular));
  EXPECT_TRUE(service_.exceptions().empty());

  manager.reset();
  base::RunLoop().RunUntilIdle();
  task_environment_.RunUntilIdle();
  EXPECT_EQ(std::set<std::string>{"news.test"},
            filter_list_storage::LoadState(dir_.GetPath()).cosmetic_off_hosts());
}

TEST_F(FilterListManagerTest, TheInstalledPolicyTurnsHidingOffForThatSiteOnly) {
  auto manager = MakeManager();
  const SessionKey regular = SessionKey::Regular();
  EXPECT_TRUE(service_.IsCosmeticFilteringEnabled(GURL("https://news.test/"), regular));
  ASSERT_EQ(ExceptionResult::kOk,
            manager->SetCosmeticHidingOffForTesting("news.test", true, regular));
  EXPECT_FALSE(service_.IsCosmeticFilteringEnabled(GURL("https://news.test/"), regular));
  EXPECT_FALSE(service_.IsCosmeticFilteringEnabled(GURL("https://www.news.test/"), regular));
  EXPECT_TRUE(service_.IsCosmeticFilteringEnabled(GURL("https://other.test/"), regular));
  // Network blocking is unchanged there: no blocking exception was made.
  EXPECT_FALSE(manager->HasException("news.test", regular));
  EXPECT_FALSE(service_.IsExcepted("news.test", regular));
  // Turning it back on restores hiding.
  ASSERT_EQ(ExceptionResult::kOk,
            manager->SetCosmeticHidingOffForTesting("news.test", false, regular));
  EXPECT_TRUE(service_.IsCosmeticFilteringEnabled(GURL("https://news.test/"), regular));
  // Once the manager is gone the policy fails open: hiding is on.
  ASSERT_EQ(ExceptionResult::kOk,
            manager->SetCosmeticHidingOffForTesting("news.test", true, regular));
  manager.reset();
  EXPECT_TRUE(service_.IsCosmeticFilteringEnabled(GURL("https://news.test/"), regular));
}

TEST_F(FilterListManagerTest, AnIncognitoCosmeticChoiceIsMemoryOnlyAndScopedToItsSession) {
  auto manager = MakeManager();
  const SessionKey regular = SessionKey::Regular();
  int id = 0, other_id = 0;
  const SessionKey incognito = SessionKey::OffTheRecord(&id);
  const SessionKey other_incognito = SessionKey::OffTheRecord(&other_id);
  ASSERT_EQ(ExceptionResult::kOk,
            manager->SetCosmeticHidingOffForTesting("News.Test", true, incognito));
  EXPECT_TRUE(manager->IsCosmeticHidingOff("news.test", incognito));
  EXPECT_FALSE(manager->IsCosmeticHidingOff("news.test", regular));
  EXPECT_FALSE(manager->IsCosmeticHidingOff("news.test", other_incognito));
  EXPECT_TRUE(manager->state().cosmetic_off_hosts().empty());
  EXPECT_FALSE(service_.IsCosmeticFilteringEnabled(GURL("https://news.test/"), incognito));
  EXPECT_TRUE(service_.IsCosmeticFilteringEnabled(GURL("https://news.test/"), regular));

  manager->SetCosmeticHidingOffForTesting("regular.test", true, regular);  // Saves.
  manager.reset();
  base::RunLoop().RunUntilIdle();
  task_environment_.RunUntilIdle();
  std::string file;
  ASSERT_TRUE(base::ReadFileToString(dir_.GetPath().AppendASCII("state.json"), &file));
  EXPECT_EQ(std::string::npos, file.find("news.test"));
  EXPECT_NE(std::string::npos, file.find("regular.test"));
}

TEST_F(FilterListManagerTest, ClearingASessionDropsItsCosmeticChoices) {
  auto manager = MakeManager();
  int id = 0;
  const SessionKey incognito = SessionKey::OffTheRecord(&id);
  manager->SetCosmeticHidingOffForTesting("news.test", true, incognito);
  manager->ClearSessionExceptions(incognito);
  EXPECT_FALSE(manager->IsCosmeticHidingOff("news.test", incognito));
  EXPECT_TRUE(service_.IsCosmeticFilteringEnabled(GURL("https://news.test/"), incognito));
}

TEST_F(FilterListManagerTest, IncognitoCannotTurnHidingBackOnForASavedChoice) {
  auto manager = MakeManager();
  int id = 0;
  const SessionKey incognito = SessionKey::OffTheRecord(&id);
  ASSERT_EQ(ExceptionResult::kOk,
            manager->SetCosmeticHidingOffForTesting("news.test", true, SessionKey::Regular()));
  EXPECT_TRUE(manager->IsCosmeticHidingOff("news.test", incognito));
  EXPECT_TRUE(manager->IsCosmeticHidingLocked("news.test", incognito));
  EXPECT_FALSE(manager->IsCosmeticHidingLocked("news.test", SessionKey::Regular()));
  EXPECT_EQ(ExceptionResult::kSavedInRegularProfile,
            manager->SetCosmeticHidingOffForTesting("news.test", false, incognito));
  EXPECT_TRUE(manager->IsCosmeticHidingOff("news.test", incognito));
  EXPECT_EQ(ExceptionResult::kOk,
            manager->SetCosmeticHidingOffForTesting("news.test", false, SessionKey::Regular()));
  EXPECT_FALSE(manager->IsCosmeticHidingOff("news.test", incognito));
}

TEST_F(FilterListManagerTest, AFullCosmeticTableRefusesNewHostsButStillRemoves) {
  auto manager = MakeManager();
  int id = 0;
  const SessionKey regular = SessionKey::Regular();
  const SessionKey incognito = SessionKey::OffTheRecord(&id);
  for (size_t i = 0; i < kMaxCosmeticOffHosts; ++i) {
    const std::string host = "host" + std::to_string(i) + ".test";
    ASSERT_EQ(ExceptionResult::kOk, manager->SetCosmeticHidingOffForTesting(host, true, regular));
    ASSERT_EQ(ExceptionResult::kOk,
              manager->SetCosmeticHidingOffForTesting(host + "x", true, incognito));
  }
  EXPECT_EQ(ExceptionResult::kTableFull,
            manager->SetCosmeticHidingOffForTesting("one-more.test", true, regular));
  EXPECT_EQ(ExceptionResult::kTableFull,
            manager->SetCosmeticHidingOffForTesting("one-more.test", true, incognito));
  EXPECT_EQ(ExceptionResult::kOk,
            manager->SetCosmeticHidingOffForTesting("host0.test", false, regular));
  EXPECT_EQ(ExceptionResult::kOk,
            manager->SetCosmeticHidingOffForTesting("host0.testx", false, incognito));
}

TEST_F(FilterListManagerTest, TheDefaultLevelIsAggressiveAndSurvivesARestart) {
  auto manager = MakeManager();
  const SessionKey regular = SessionKey::Regular();
  EXPECT_EQ(BlockingLevel::kAggressive, manager->DefaultBlockingLevel());
  EXPECT_EQ(BlockingLevel::kAggressive,
            manager->EffectiveBlockingLevel("news.test", regular));
  manager->SetDefaultBlockingLevel(BlockingLevel::kStandard);
  EXPECT_EQ(BlockingLevel::kStandard,
            manager->EffectiveBlockingLevel("news.test", regular));
  manager.reset();
  base::RunLoop().RunUntilIdle();
  task_environment_.RunUntilIdle();
  EXPECT_EQ(BlockingLevel::kStandard,
            filter_list_storage::LoadState(dir_.GetPath()).default_level());
}

TEST_F(FilterListManagerTest, ASiteLevelCoversSubdomainsAndBeatsTheDefault) {
  auto manager = MakeManager();
  const SessionKey regular = SessionKey::Regular();
  ASSERT_EQ(ExceptionResult::kOk,
            manager->SetBlockingLevelForTesting("News.Test", BlockingLevel::kStandard, regular));
  EXPECT_EQ(BlockingLevel::kStandard, manager->EffectiveBlockingLevel("news.test", regular));
  EXPECT_EQ(BlockingLevel::kStandard, manager->EffectiveBlockingLevel("www.news.test", regular));
  EXPECT_EQ(BlockingLevel::kAggressive, manager->EffectiveBlockingLevel("notnews.test", regular));
  manager->SetDefaultBlockingLevel(BlockingLevel::kStandard);
  ASSERT_EQ(ExceptionResult::kOk,
            manager->SetBlockingLevelForTesting("news.test", BlockingLevel::kAggressive, regular));
  EXPECT_EQ(BlockingLevel::kAggressive, manager->EffectiveBlockingLevel("news.test", regular));
  EXPECT_EQ(BlockingLevel::kStandard, manager->EffectiveBlockingLevel("other.test", regular));
  EXPECT_EQ(ExceptionResult::kInvalidHost,
            manager->SetBlockingLevelForTesting("not a host", BlockingLevel::kStandard, regular));
}

TEST_F(FilterListManagerTest, AnIncognitoLevelIsMemoryOnlyAndScopedToItsSession) {
  auto manager = MakeManager();
  const SessionKey regular = SessionKey::Regular();
  int id = 0, other_id = 0;
  const SessionKey incognito = SessionKey::OffTheRecord(&id);
  const SessionKey other_incognito = SessionKey::OffTheRecord(&other_id);
  ASSERT_EQ(ExceptionResult::kOk,
            manager->SetBlockingLevelForTesting("news.test", BlockingLevel::kStandard, incognito));
  EXPECT_EQ(BlockingLevel::kStandard, manager->EffectiveBlockingLevel("news.test", incognito));
  EXPECT_EQ(BlockingLevel::kAggressive, manager->EffectiveBlockingLevel("news.test", regular));
  EXPECT_EQ(BlockingLevel::kAggressive, manager->EffectiveBlockingLevel("news.test", other_incognito));
  EXPECT_TRUE(manager->state().level_overrides().empty());
  // Its own choice wins over a saved one, for that session only.
  manager->SetBlockingLevelForTesting("saved.test", BlockingLevel::kStandard, regular);
  EXPECT_EQ(BlockingLevel::kStandard, manager->EffectiveBlockingLevel("saved.test", incognito));
  ASSERT_EQ(ExceptionResult::kOk,
            manager->SetBlockingLevelForTesting("saved.test", BlockingLevel::kAggressive, incognito));
  EXPECT_EQ(BlockingLevel::kAggressive, manager->EffectiveBlockingLevel("saved.test", incognito));
  EXPECT_EQ(BlockingLevel::kStandard, manager->EffectiveBlockingLevel("saved.test", regular));
  manager.reset();
  base::RunLoop().RunUntilIdle();
  task_environment_.RunUntilIdle();
  std::string file;
  ASSERT_TRUE(base::ReadFileToString(dir_.GetPath().AppendASCII("state.json"), &file));
  EXPECT_EQ(std::string::npos, file.find("news.test"));
  EXPECT_NE(std::string::npos, file.find("saved.test"));
}

TEST_F(FilterListManagerTest, ClearingASessionDropsItsLevels) {
  auto manager = MakeManager();
  int id = 0;
  const SessionKey incognito = SessionKey::OffTheRecord(&id);
  manager->SetBlockingLevelForTesting("news.test", BlockingLevel::kStandard, incognito);
  manager->ClearSessionExceptions(incognito);
  EXPECT_EQ(BlockingLevel::kAggressive, manager->EffectiveBlockingLevel("news.test", incognito));
}

TEST_F(FilterListManagerTest, AFullLevelTableRefusesNewHostsButStillChangesOldOnes) {
  auto manager = MakeManager();
  int id = 0;
  const SessionKey regular = SessionKey::Regular();
  const SessionKey incognito = SessionKey::OffTheRecord(&id);
  for (size_t i = 0; i < kMaxLevelOverrides; ++i) {
    const std::string host = "host" + std::to_string(i) + ".test";
    ASSERT_EQ(ExceptionResult::kOk,
              manager->SetBlockingLevelForTesting(host, BlockingLevel::kStandard, regular));
    ASSERT_EQ(ExceptionResult::kOk,
              manager->SetBlockingLevelForTesting(host + "x", BlockingLevel::kStandard, incognito));
  }
  EXPECT_EQ(ExceptionResult::kTableFull,
            manager->SetBlockingLevelForTesting("one-more.test", BlockingLevel::kStandard, regular));
  EXPECT_EQ(ExceptionResult::kTableFull,
            manager->SetBlockingLevelForTesting("one-more.test", BlockingLevel::kStandard, incognito));
  EXPECT_EQ(ExceptionResult::kOk,
            manager->SetBlockingLevelForTesting("host0.test", BlockingLevel::kAggressive, regular));
}

TEST_F(FilterListManagerTest, TheInstalledPolicyDecidesWhetherAFirstPartyMatchIsBlocked) {
  auto manager = MakeManager();
  const SessionKey regular = SessionKey::Regular();
  // "shipped.example" is a rule of the shipped engine.
  auto first_party_blocked = [&](const char* top_host) {
    ClassifiedRequest request{"https://shipped.example/a.js", "shipped.example",
                              top_host, RequestKind::kScript, false};
    bool answer = false;
    base::RunLoop loop;
    service_.ShouldBlock(request, base::BindLambdaForTesting([&](bool block) {
                           answer = block;
                           loop.Quit();
                         }));
    loop.Run();
    return answer;
  };
  EXPECT_TRUE(first_party_blocked("shipped.example"));
  ASSERT_EQ(ExceptionResult::kOk,
            manager->SetBlockingLevelForTesting("shipped.example", BlockingLevel::kStandard, regular));
  EXPECT_FALSE(first_party_blocked("shipped.example"));
  // Third-party requests are blocked in both levels.
  EXPECT_TRUE(Blocks("shipped.example"));
  manager->SetDefaultBlockingLevel(BlockingLevel::kStandard);
  EXPECT_TRUE(Blocks("shipped.example"));
  // Once the manager is gone the level is Aggressive.
  manager.reset();
  EXPECT_EQ(BlockingLevel::kAggressive, service_.BlockingLevelFor("shipped.example", regular));
  EXPECT_TRUE(first_party_blocked("shipped.example"));
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

  manager->MaybeRunScheduledUpdate(base::Time::Now());  // Install day: starts the clock.
  task_environment_.AdvanceClock(base::Hours(23));
  manager->MaybeRunScheduledUpdate(base::Time::Now());
  WaitUntilIdle(*manager);
  EXPECT_EQ(1u, network_.total_requests());

  task_environment_.AdvanceClock(base::Hours(2));
  manager->MaybeRunScheduledUpdate(base::Time::Now());
  WaitUntilIdle(*manager);
  EXPECT_EQ(4u, network_.total_requests());
  EXPECT_TRUE(Blocks("new-one.example"));
  EXPECT_TRUE(Blocks("custom.example"));
  EXPECT_FALSE(Blocks("builtin-one.example"));  // Replaced by the new copy.

  // Not again until another day has passed.
  task_environment_.AdvanceClock(base::Hours(1));
  manager->MaybeRunScheduledUpdate(base::Time::Now());
  WaitUntilIdle(*manager);
  EXPECT_EQ(4u, network_.total_requests());

  task_environment_.AdvanceClock(base::Hours(24));
  manager->MaybeRunScheduledUpdate(base::Time::Now());
  WaitUntilIdle(*manager);
  EXPECT_EQ(7u, network_.total_requests());
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

TEST_F(FilterListManagerTest, TheShippedListsHaveARealRuleCount) {
  auto manager = MakeManager();
  for (const FilterListEntry& entry : manager->state().lists()) {
    EXPECT_EQ(1, entry.rules) << entry.id;
    EXPECT_TRUE(entry.updated.is_null()) << entry.id;
  }
  EXPECT_EQ(0u, network_.total_requests());
}

TEST_F(FilterListManagerTest, TurningEasyPrivacyOffTakesItOutOfTheEngine) {
  auto manager = MakeManager();
  ASSERT_EQ(FilterListState::EnableResult::kOk,
            manager->SetListEnabled(kEasyPrivacyId, false));
  WaitUntilIdle(*manager);
  EXPECT_TRUE(Blocks("builtin-one.example"));
  EXPECT_FALSE(Blocks("builtin-two.example"));
  EXPECT_FALSE(manager->state().Find(kEasyPrivacyId)->enabled);

  ASSERT_EQ(FilterListState::EnableResult::kOk,
            manager->SetListEnabled(kEasyPrivacyId, true));
  WaitUntilIdle(*manager);
  EXPECT_TRUE(Blocks("builtin-two.example"));
}

TEST_F(FilterListManagerTest, EasyListCannotBeTurnedOff) {
  auto manager = MakeManager();
  EXPECT_EQ(FilterListState::EnableResult::kRequired,
            manager->SetListEnabled(kEasyListId, false));
  WaitUntilIdle(*manager);
  EXPECT_TRUE(manager->state().Find(kEasyListId)->enabled);
  // The refusal rebuilds nothing, so the shipped engine still runs.
  EXPECT_TRUE(Blocks("shipped.example"));
  EXPECT_EQ(FilterListState::EnableResult::kUnknownList,
            manager->SetListEnabled("nosuchlist", false));
}

TEST_F(FilterListManagerTest, TurningAListOffKeepsItsFile) {
  network_.AddResponse(kCustomUrl, "||custom.example^\n");
  auto manager = MakeManager();
  std::string error;
  ASSERT_TRUE(manager->AddList(GURL(kCustomUrl), &error));
  WaitUntilIdle(*manager);
  const std::string id = manager->state().lists().back().id;
  const base::FilePath file = filter_list_storage::ListPath(dir_.GetPath(), id);
  ASSERT_TRUE(base::PathExists(file));

  manager->SetListEnabled(id, false);
  WaitUntilIdle(*manager);
  EXPECT_FALSE(Blocks("custom.example"));
  EXPECT_TRUE(base::PathExists(file));
  ASSERT_TRUE(manager->state().Find(id));  // Still listed.

  manager->SetListEnabled(id, true);
  WaitUntilIdle(*manager);
  EXPECT_TRUE(Blocks("custom.example"));
  // Removal is what deletes the file.
  manager->RemoveList(id);
  WaitUntilIdle(*manager);
  EXPECT_FALSE(base::PathExists(file));
}

TEST_F(FilterListManagerTest, AListThatIsOffStaysOffAfterARestart) {
  {
    auto manager = MakeManager();
    manager->SetListEnabled(kEasyPrivacyId, false);
    WaitUntilIdle(*manager);
  }
  auto manager = MakeManager();
  EXPECT_FALSE(manager->state().Find(kEasyPrivacyId)->enabled);
  EXPECT_TRUE(Blocks("builtin-one.example"));
  EXPECT_FALSE(Blocks("builtin-two.example"));
}

TEST_F(FilterListManagerTest, UpdateAllSkipsAListThatIsOff) {
  network_.AddResponse(kEasyListUrl, "||new-one.example^\n");
  network_.AddResponse(kEasyPrivacyUrl, "||new-two.example^\n");
  auto manager = MakeManager();
  manager->SetListEnabled(kEasyPrivacyId, false);
  WaitUntilIdle(*manager);
  manager->UpdateAll();
  WaitUntilIdle(*manager);
  EXPECT_EQ(1u, network_.total_requests());
  EXPECT_FALSE(manager->state().Find(kEasyPrivacyId)->enabled);
  EXPECT_FALSE(Blocks("new-two.example"));
}

TEST_F(FilterListManagerTest, UpdateStatusGoesFromUpdatingToUpdatedOrFailed) {
  network_.AddResponse(kEasyListUrl, "||new-one.example^\n");
  network_.AddResponse(kEasyPrivacyUrl, "nope", net::HTTP_NOT_FOUND);
  auto manager = MakeManager();
  EXPECT_EQ(FilterListManager::UpdateStatus::kIdle,
            manager->UpdateStatusFor(kEasyListId));

  manager->UpdateList(kEasyListId);
  manager->UpdateList(kEasyPrivacyId);
  EXPECT_EQ(FilterListManager::UpdateStatus::kUpdating,
            manager->UpdateStatusFor(kEasyListId));
  WaitUntilIdle(*manager);
  EXPECT_EQ(FilterListManager::UpdateStatus::kUpdated,
            manager->UpdateStatusFor(kEasyListId));
  EXPECT_EQ(FilterListManager::UpdateStatus::kFailed,
            manager->UpdateStatusFor(kEasyPrivacyId));
  EXPECT_EQ(ListFailureMessage(ListFailure::kDownload),
            manager->state().Find(kEasyPrivacyId)->error);

  // Retry: the same call. A good answer clears the error.
  network_.ClearResponses();
  network_.AddResponse(kEasyPrivacyUrl, "||new-two.example^\n");
  manager->UpdateList(kEasyPrivacyId);
  WaitUntilIdle(*manager);
  EXPECT_EQ(FilterListManager::UpdateStatus::kUpdated,
            manager->UpdateStatusFor(kEasyPrivacyId));
  EXPECT_EQ("", manager->state().Find(kEasyPrivacyId)->error);
}

TEST_F(FilterListManagerTest, AnUpdatedListReportsItsIgnoredExceptions) {
  network_.AddResponse(kEasyListUrl, "||new-one.example^\n@@http\n@@|http\n");
  auto manager = MakeManager();
  manager->UpdateList(kEasyListId);
  WaitUntilIdle(*manager);
  const FilterListEntry* entry = manager->state().Find(kEasyListId);
  EXPECT_EQ(1, entry->rules);
  EXPECT_EQ(2, entry->ignored_exceptions);
}

// The user's own rules (issue #107).
class CustomRulesTest : public FilterListManagerTest {
 protected:
  // Saves |text| and waits for the answer and for the rebuild.
  CustomRulesFailure Save(FilterListManager& manager, std::string text) {
    CustomRulesFailure result = CustomRulesFailure::kUnwritable;
    base::RunLoop loop;
    manager.SetCustomRules(
        std::move(text), base::BindLambdaForTesting([&](CustomRulesFailure f) {
          result = f;
          loop.Quit();
        }));
    loop.Run();
    WaitUntilIdle(manager);
    return result;
  }

  base::FilePath File() {
    return filter_list_storage::ListPath(dir_.GetPath(), kCustomRulesId);
  }
};

TEST_F(CustomRulesTest, ASavedRuleBlocksAtOnceAndNothingIsFetched) {
  auto manager = MakeManager();
  EXPECT_FALSE(Blocks("mine.example"));
  EXPECT_EQ(CustomRulesFailure::kNone,
            Save(*manager, "! mine\n||mine.example^\n"));
  EXPECT_TRUE(Blocks("mine.example"));
  EXPECT_TRUE(Blocks("builtin-one.example"));  // The other lists stay.
  const FilterListEntry* entry = manager->state().Find(kCustomRulesId);
  ASSERT_TRUE(entry);
  EXPECT_TRUE(entry->url.empty());
  EXPECT_EQ(1, entry->rules);
  EXPECT_EQ("", entry->error);
  EXPECT_FALSE(entry->updated.is_null());
  EXPECT_EQ("! mine\n||mine.example^\n", manager->custom_rules().text);
  EXPECT_EQ(1, manager->custom_rules().network_rules);
  std::string saved;
  ASSERT_TRUE(base::ReadFileToString(File(), &saved));
  EXPECT_EQ("! mine\n||mine.example^\n", saved);
  EXPECT_EQ(0u, network_.total_requests());
}

TEST_F(CustomRulesTest,
       AScriptletLineIsDroppedWithAReasonAndTheRestStillWorks) {
  auto manager = MakeManager();
  ASSERT_EQ(
      CustomRulesFailure::kNone,
      Save(*manager,
           "||mine.example^\na.localhost##+js(abort-on-property-read, x)\n"));
  EXPECT_TRUE(Blocks("mine.example"));
  const CustomRules& rules = manager->custom_rules();
  ASSERT_EQ(1u, rules.issues.size());
  EXPECT_EQ(2, rules.issues[0].line);
  EXPECT_EQ("scriptlet", rules.issues[0].reason);
  EXPECT_FALSE(rules.issues[0].ignored);
  EXPECT_EQ("a.localhost##+js(abort-on-property-read, x)",
            rules.issues[0].rule);
  EXPECT_EQ(1, rules.dropped);
}

TEST_F(CustomRulesTest, ABroadExceptionIsReportedAsIgnored) {
  auto manager = MakeManager();
  ASSERT_EQ(CustomRulesFailure::kNone,
            Save(*manager, "||mine.example^\n@@http\n"));
  const CustomRules& rules = manager->custom_rules();
  ASSERT_EQ(1u, rules.issues.size());
  EXPECT_EQ("matches_every_address", rules.issues[0].reason);
  EXPECT_TRUE(rules.issues[0].ignored);
  EXPECT_EQ(1, rules.ignored_exceptions);
  EXPECT_EQ(1, manager->state().Find(kCustomRulesId)->ignored_exceptions);
}

TEST_F(CustomRulesTest, ATextOverTheLimitsIsRefusedAndChangesNothing) {
  auto manager = MakeManager();
  ASSERT_EQ(CustomRulesFailure::kNone, Save(*manager, "||mine.example^\n"));

  EXPECT_EQ(CustomRulesFailure::kTooLarge,
            Save(*manager, std::string(256 * 1024 + 1, '!')));
  std::string many;
  for (int i = 0; i < 10001; ++i) {
    many += "!\n";
  }
  EXPECT_EQ(CustomRulesFailure::kTooManyLines, Save(*manager, many));
  EXPECT_EQ(CustomRulesFailure::kNotText,
            Save(*manager, "||x.example^\n\xff\n"));

  EXPECT_TRUE(Blocks("mine.example"));
  EXPECT_EQ("||mine.example^\n", manager->custom_rules().text);
  std::string saved;
  ASSERT_TRUE(base::ReadFileToString(File(), &saved));
  EXPECT_EQ("||mine.example^\n", saved);
}

TEST_F(CustomRulesTest, HostileLinesAreDroppedAndTheFileStaysSmall) {
  auto manager = MakeManager();
  std::string text = "||mine.example^\n";
  text += "||" + std::string(5000, 'a') + ".example^\n";    // Over 4 KiB.
  text += std::string("||nul") + '\0' + "byte.example^\n";  // A NUL byte.
  text += "/(a+)+$/\n";                                     // A regex bomb.
  ASSERT_EQ(CustomRulesFailure::kNone, Save(*manager, text));
  const CustomRules& rules = manager->custom_rules();
  // The engine does not accept the regex line as a network rule, so the report
  // names it too, as "unparseable".
  ASSERT_EQ(3u, rules.issues.size());
  EXPECT_EQ("too_long", rules.issues[0].reason);
  EXPECT_LE(rules.issues[0].rule.size(), 200u);
  EXPECT_EQ("contains_nul", rules.issues[1].reason);
  EXPECT_EQ("unparseable", rules.issues[2].reason);
  EXPECT_TRUE(Blocks("mine.example"));
}

TEST_F(CustomRulesTest, RulesThatAreOnlyHideRulesDoNotBreakTheBuild) {
  auto manager = MakeManager();
  ASSERT_EQ(CustomRulesFailure::kNone, Save(*manager, "##.banner\n"));
  const FilterListEntry* entry = manager->state().Find(kCustomRulesId);
  ASSERT_TRUE(entry);
  EXPECT_EQ("", entry->error);
  EXPECT_FALSE(entry->updated.is_null());
  EXPECT_TRUE(Blocks("builtin-one.example"));  // The rebuild worked.
  EXPECT_EQ(1, manager->custom_rules().hide_rules);
}

TEST_F(CustomRulesTest, TheRulesSurviveARestartAndAreAnalysedAgain) {
  {
    auto manager = MakeManager();
    ASSERT_EQ(CustomRulesFailure::kNone,
              Save(*manager, "||mine.example^\nb.test#%#//scriptlet('x')\n"));
  }
  auto manager = MakeManager();
  EXPECT_TRUE(Blocks("mine.example"));
  EXPECT_EQ("||mine.example^\nb.test#%#//scriptlet('x')\n",
            manager->custom_rules().text);
  ASSERT_EQ(1u, manager->custom_rules().issues.size());
  EXPECT_EQ("scriptlet", manager->custom_rules().issues[0].reason);
}

TEST_F(CustomRulesTest, ABlankTextRemovesTheRulesAndTheFile) {
  auto manager = MakeManager();
  ASSERT_EQ(CustomRulesFailure::kNone, Save(*manager, "||mine.example^\n"));
  ASSERT_TRUE(base::PathExists(File()));
  EXPECT_EQ(CustomRulesFailure::kNone, Save(*manager, " \n\t\n"));
  EXPECT_FALSE(Blocks("mine.example"));
  EXPECT_EQ(nullptr, manager->state().Find(kCustomRulesId));
  EXPECT_FALSE(base::PathExists(File()));
  EXPECT_EQ("", manager->custom_rules().text);
  EXPECT_FALSE(manager->state().NeedsRebuild());
}

TEST_F(CustomRulesTest, TurningTheRulesOffKeepsThemAndStopsThem) {
  auto manager = MakeManager();
  ASSERT_EQ(CustomRulesFailure::kNone, Save(*manager, "||mine.example^\n"));
  EXPECT_EQ(FilterListState::EnableResult::kOk,
            manager->SetListEnabled(kCustomRulesId, false));
  WaitUntilIdle(*manager);
  EXPECT_FALSE(Blocks("mine.example"));
  EXPECT_TRUE(base::PathExists(File()));
  // A new save keeps the switch where the user put it.
  ASSERT_EQ(CustomRulesFailure::kNone,
            Save(*manager, "||mine.example^\n||two.example^\n"));
  EXPECT_FALSE(manager->state().Find(kCustomRulesId)->enabled);
  EXPECT_FALSE(Blocks("two.example"));
}

TEST_F(CustomRulesTest, UpdateAllAndUpdateListNeverFetchTheUsersOwnRules) {
  network_.AddResponse(kEasyListUrl, "||new-one.example^\n");
  network_.AddResponse(kEasyPrivacyUrl, "||new-two.example^\n");
  auto manager = MakeManager();
  ASSERT_EQ(CustomRulesFailure::kNone, Save(*manager, "||mine.example^\n"));
  manager->UpdateAll();
  manager->UpdateList(kCustomRulesId);
  WaitUntilIdle(*manager);
  EXPECT_EQ(2u, network_.total_requests());
  EXPECT_TRUE(Blocks("mine.example"));
  EXPECT_EQ(FilterListManager::UpdateStatus::kIdle,
            manager->UpdateStatusFor(kCustomRulesId));
}

TEST_F(CustomRulesTest, RemovingTheListClearsTheRules) {
  auto manager = MakeManager();
  ASSERT_EQ(CustomRulesFailure::kNone, Save(*manager, "||mine.example^\n"));
  manager->RemoveList(kCustomRulesId);
  WaitUntilIdle(*manager);
  EXPECT_FALSE(Blocks("mine.example"));
  EXPECT_EQ("", manager->custom_rules().text);
  EXPECT_FALSE(base::PathExists(File()));
}

TEST_F(CustomRulesTest, TheLastOfTwoQuickSavesWins) {
  auto manager = MakeManager();
  std::vector<CustomRulesFailure> results;
  base::RunLoop loop;
  auto done = base::BindLambdaForTesting([&](CustomRulesFailure f) {
    results.push_back(f);
    if (results.size() == 2) {
      loop.Quit();
    }
  });
  manager->SetCustomRules("||first.example^\n", done);
  manager->SetCustomRules("||second.example^\n", done);
  loop.Run();
  WaitUntilIdle(*manager);
  EXPECT_FALSE(Blocks("first.example"));
  EXPECT_TRUE(Blocks("second.example"));
  EXPECT_EQ("||second.example^\n", manager->custom_rules().text);
}


// The catalogue (issue #105).

constexpr char kFanboyUrl[] = "https://easylist.to/easylist/fanboy-annoyance.txt";
constexpr char kUboPrivacyUrl[] =
    "https://ublockorigin.github.io/uAssets/filters/privacy.min.txt";

TEST_F(FilterListManagerTest, ACatalogueListIsDownloadedFromTheBuildTimeAddressWhenTurnedOn) {
  network_.AddResponse(kFanboyUrl, "! Title: Some other name\n||cat-one.example^\n");
  std::vector<std::string> requested;
  network_.SetInterceptor(base::BindLambdaForTesting(
      [&](const network::ResourceRequest& request) {
        requested.push_back(request.url.spec());
      }));
  auto manager = MakeManager();
  EXPECT_EQ(0u, network_.total_requests());  // Nothing by default.

  EXPECT_EQ(FilterListManager::CatalogueResult::kOk,
            manager->SetCatalogueListEnabled("fanboyannoyances", true));
  WaitUntilIdle(*manager);
  EXPECT_EQ(1u, network_.total_requests());
  EXPECT_EQ((std::vector<std::string>{kFanboyUrl}), requested);
  EXPECT_TRUE(Blocks("cat-one.example"));
  const FilterListEntry* entry = manager->state().Find("fanboyannoyances");
  ASSERT_TRUE(entry);
  EXPECT_TRUE(entry->catalogue);
  EXPECT_FALSE(entry->updated.is_null());
  EXPECT_GT(entry->bytes, 0);
  EXPECT_EQ(1, entry->rules);
  EXPECT_EQ("", entry->error);
  // The list's own header does not rename a catalogue list.
  EXPECT_NE("Some other name", entry->title);
}

TEST_F(FilterListManagerTest, AnUnknownIdOrARawAddressIsRefusedAndNothingIsFetched) {
  auto manager = MakeManager();
  for (const char* bad :
       {"nope", "", "easylist", "custom", "FANBOYANNOYANCES", kFanboyUrl,
        "https://evil.example/list.txt", "http://127.0.0.1:8000/list.txt"}) {
    EXPECT_EQ(FilterListManager::CatalogueResult::kUnknownId,
              manager->SetCatalogueListEnabled(bad, true))
        << bad;
    EXPECT_EQ(FilterListManager::CatalogueResult::kUnknownId,
              manager->SetCatalogueListEnabled(bad, false))
        << bad;
  }
  WaitUntilIdle(*manager);
  EXPECT_FALSE(manager->busy());
  EXPECT_EQ(0u, network_.total_requests());
  EXPECT_EQ(0, network_.NumPending());
  EXPECT_EQ(2u, manager->state().lists().size());
}

TEST_F(FilterListManagerTest, TurningACatalogueListOffRemovesItAndStopsItsDownloads) {
  network_.AddResponse(kFanboyUrl, "||cat-one.example^\n");
  network_.AddResponse(kEasyListUrl, "||new-one.example^\n");
  network_.AddResponse(kEasyPrivacyUrl, "||new-two.example^\n");
  std::vector<std::string> requested;
  network_.SetInterceptor(base::BindLambdaForTesting(
      [&](const network::ResourceRequest& request) {
        requested.push_back(request.url.spec());
      }));
  auto manager = MakeManager();
  ASSERT_EQ(FilterListManager::CatalogueResult::kOk,
            manager->SetCatalogueListEnabled("fanboyannoyances", true));
  WaitUntilIdle(*manager);
  const base::FilePath file =
      filter_list_storage::ListPath(dir_.GetPath(), "fanboyannoyances");
  ASSERT_TRUE(base::PathExists(file));
  ASSERT_TRUE(Blocks("cat-one.example"));

  EXPECT_EQ(FilterListManager::CatalogueResult::kOk,
            manager->SetCatalogueListEnabled("fanboyannoyances", false));
  WaitUntilIdle(*manager);
  EXPECT_EQ(nullptr, manager->state().Find("fanboyannoyances"));
  EXPECT_FALSE(base::PathExists(file));
  EXPECT_FALSE(Blocks("cat-one.example"));

  // The schedule has nothing of it to fetch.
  manager->MaybeRunScheduledUpdate(base::Time::Now());
  task_environment_.AdvanceClock(base::Days(2));
  manager->MaybeRunScheduledUpdate(base::Time::Now());
  WaitUntilIdle(*manager);
  EXPECT_EQ((std::vector<std::string>{kFanboyUrl, kEasyListUrl, kEasyPrivacyUrl}),
            requested);  // After the one download, only the built-in lists.
}

TEST_F(FilterListManagerTest, ACatalogueListThatIsOnIsUpdatedOnTheDailySchedule) {
  network_.AddResponse(kFanboyUrl, "||cat-one.example^\n");
  network_.AddResponse(kEasyListUrl, "||new-one.example^\n");
  network_.AddResponse(kEasyPrivacyUrl, "||new-two.example^\n");
  auto manager = MakeManager();
  ASSERT_EQ(FilterListManager::CatalogueResult::kOk,
            manager->SetCatalogueListEnabled("fanboyannoyances", true));
  WaitUntilIdle(*manager);
  ASSERT_EQ(1u, network_.total_requests());

  manager->MaybeRunScheduledUpdate(base::Time::Now());
  task_environment_.AdvanceClock(base::Hours(23));
  manager->MaybeRunScheduledUpdate(base::Time::Now());
  WaitUntilIdle(*manager);
  EXPECT_EQ(1u, network_.total_requests());  // Not before a day.

  task_environment_.AdvanceClock(base::Hours(2));
  manager->MaybeRunScheduledUpdate(base::Time::Now());
  WaitUntilIdle(*manager);
  EXPECT_EQ(4u, network_.total_requests());  // Two built-in and the catalogue list.
}

TEST_F(FilterListManagerTest, ACatalogueListSurvivesARestartWithNoRequest) {
  network_.AddResponse(kFanboyUrl, "||cat-one.example^\n");
  {
    auto manager = MakeManager();
    ASSERT_EQ(FilterListManager::CatalogueResult::kOk,
              manager->SetCatalogueListEnabled("fanboyannoyances", true));
    WaitUntilIdle(*manager);
  }
  const size_t before = network_.total_requests();
  auto manager = MakeManager();
  const FilterListEntry* entry = manager->state().Find("fanboyannoyances");
  ASSERT_TRUE(entry);
  EXPECT_TRUE(entry->catalogue);
  EXPECT_EQ(kFanboyUrl, entry->url);
  EXPECT_TRUE(Blocks("cat-one.example"));
  EXPECT_EQ(before, network_.total_requests());
}

TEST_F(FilterListManagerTest, TurningAListOnTwiceFetchesOnce) {
  network_.AddResponse(kFanboyUrl, "||cat-one.example^\n");
  auto manager = MakeManager();
  EXPECT_EQ(FilterListManager::CatalogueResult::kOk,
            manager->SetCatalogueListEnabled("fanboyannoyances", true));
  EXPECT_EQ(FilterListManager::CatalogueResult::kOk,
            manager->SetCatalogueListEnabled("fanboyannoyances", true));
  WaitUntilIdle(*manager);
  EXPECT_EQ(1u, network_.total_requests());
}

// What counts towards kMaxListsInUse: every list but the user's own rules.
size_t InUse(const FilterListState& state) {
  size_t n = 0;
  for (const FilterListEntry& entry : state.lists()) {
    n += entry.id != kCustomRulesId;
  }
  return n;
}

// A manager over a state file the test wrote first.
class CatalogueCapTest : public FilterListManagerTest {
 protected:
  void Seed(const FilterListState& state) {
    ASSERT_TRUE(filter_list_storage::SaveState(dir_.GetPath(), state));
  }
};

TEST_F(CatalogueCapTest, TheNumberOfListsInUseIsCappedAndNothingIsFetchedPastIt) {
  FilterListState state;
  std::string id;
  for (size_t i = 0; i < kMaxCustomLists; ++i) {
    ASSERT_EQ(FilterListState::AddResult::kAdded,
              state.AddCustomList(GURL("https://l.example/" + std::to_string(i)),
                                  false, &id));
  }
  for (const CatalogueEntry& entry : Catalogue()) {
    if (InUse(state) < kMaxListsInUse) {
      ASSERT_EQ(FilterListState::CatalogueAddResult::kAdded,
                state.AddCatalogueList(entry.id));
    }
  }
  ASSERT_EQ(kMaxListsInUse, InUse(state));
  Seed(state);
  auto manager = MakeManager();
  ASSERT_EQ(kMaxListsInUse, InUse(manager->state()));
  const size_t before = network_.total_requests();

  std::string_view left_out;
  for (const CatalogueEntry& entry : Catalogue()) {
    if (!manager->state().Find(entry.id)) {
      left_out = entry.id;
      break;
    }
  }
  ASSERT_FALSE(left_out.empty());
  EXPECT_EQ(FilterListManager::CatalogueResult::kTooMany,
            manager->SetCatalogueListEnabled(left_out, true));
  std::string error;
  EXPECT_FALSE(manager->AddList(GURL("https://l.example/one-more"), &error));
  WaitUntilIdle(*manager);
  EXPECT_EQ(nullptr, manager->state().Find(left_out));
  EXPECT_EQ(before, network_.total_requests());
}

std::string BigList(size_t bytes) {
  std::string text = "||cat-big.example^\n";
  const std::string comment = "! padding padding padding padding padding\n";
  while (text.size() < bytes) {
    text += comment;
  }
  return text;
}

TEST_F(CatalogueCapTest, ADownloadThatTakesTheTotalSizeOverTheCapIsRefused) {
  FilterListState state;
  // Six lists at the size limit already fill 30 of the 32 MiB.
  size_t seeded = 0;
  for (const CatalogueEntry& entry : Catalogue()) {
    if (entry.id == "uboprivacy" || seeded == 6) {
      continue;
    }
    ASSERT_EQ(FilterListState::CatalogueAddResult::kAdded,
              state.AddCatalogueList(entry.id));
    state.Find(entry.id)->bytes = static_cast<int>(kMaxListBytes);
    ++seeded;
  }
  Seed(state);
  network_.AddResponse(kUboPrivacyUrl, BigList(3 * 1024 * 1024));
  auto manager = MakeManager();
  ASSERT_EQ(FilterListManager::CatalogueResult::kOk,
            manager->SetCatalogueListEnabled("uboprivacy", true));
  WaitUntilIdle(*manager);
  const FilterListEntry* entry = manager->state().Find("uboprivacy");
  ASSERT_TRUE(entry);
  EXPECT_TRUE(entry->updated.is_null());  // No copy was taken.
  EXPECT_EQ(ListFailureMessage(ListFailure::kTooLarge), entry->error);
  EXPECT_FALSE(Blocks("cat-big.example"));
  EXPECT_FALSE(base::PathExists(
      filter_list_storage::ListPath(dir_.GetPath(), "uboprivacy")));

  // A smaller list fits.
  network_.ClearResponses();
  network_.AddResponse(kUboPrivacyUrl, BigList(1024 * 1024));
  manager->UpdateList("uboprivacy");
  WaitUntilIdle(*manager);
  EXPECT_TRUE(Blocks("cat-big.example"));
  EXPECT_EQ("", manager->state().Find("uboprivacy")->error);
}

// A list of about |bytes| that blocks |host|.
std::string SizedList(const std::string& host, size_t bytes) {
  std::string text = "||" + host + "^\n";
  const std::string comment = "! padding padding padding padding padding\n";
  while (text.size() + comment.size() <= bytes) {
    text += comment;
  }
  return text;
}

constexpr size_t kBigList = kMaxListBytes - 1024;  // Fits the one-list cap.

// The ids of the first |n| lists of the catalogue, in table order.
std::vector<std::string> FirstCatalogueIds(size_t n) {
  std::vector<std::string> ids;
  for (const CatalogueEntry& entry : Catalogue()) {
    if (ids.size() < n) {
      ids.emplace_back(entry.id);
    }
  }
  return ids;
}

std::string UrlOf(const std::string& id) {
  return std::string(FindCatalogueEntry(id)->url);
}

TEST_F(CatalogueCapTest, AListTurnedBackOnCannotTakeTheTotalOverTheCap) {
  // Six lists at the size limit fill 30 of the 32 MiB; a seventh is off.
  const std::vector<std::string> ids = FirstCatalogueIds(7);
  FilterListState state;
  for (const std::string& id : ids) {
    ASSERT_EQ(FilterListState::CatalogueAddResult::kAdded,
              state.AddCatalogueList(id));
    state.Find(id)->bytes = static_cast<int>(kMaxListBytes);
  }
  state.Find(ids[6])->enabled = false;
  Seed(state);
  auto manager = MakeManager();
  const size_t before = network_.total_requests();

  // Through the catalogue's door and through the plain switch.
  EXPECT_EQ(FilterListManager::CatalogueResult::kTooLarge,
            manager->SetCatalogueListEnabled(ids[6], true));
  EXPECT_EQ(FilterListState::EnableResult::kTooLarge,
            manager->SetListEnabled(ids[6], true));
  WaitUntilIdle(*manager);
  const FilterListEntry* entry = manager->state().Find(ids[6]);
  ASSERT_TRUE(entry);
  EXPECT_FALSE(entry->enabled);
  EXPECT_EQ(ListFailureMessage(ListFailure::kTooLarge), entry->error);
  EXPECT_EQ(before, network_.total_requests());

  // Room made by turning another list off lets it back on.
  ASSERT_EQ(FilterListState::EnableResult::kOk,
            manager->SetListEnabled(ids[0], false));
  EXPECT_EQ(FilterListState::EnableResult::kOk,
            manager->SetListEnabled(ids[6], true));
  WaitUntilIdle(*manager);
  EXPECT_TRUE(manager->state().Find(ids[6])->enabled);
}

TEST_F(CatalogueCapTest, ListsTurnedOnTogetherCannotTakeTheTotalOverTheCap) {
  // Seven lists never downloaded, so none has a size on record (0 bytes).
  // Each one alone fits; together they do not.
  const std::vector<std::string> ids = FirstCatalogueIds(7);
  for (size_t i = 0; i < ids.size(); ++i) {
    network_.AddResponse(UrlOf(ids[i]),
                         SizedList("cat-" + std::to_string(i) + ".example",
                                   kBigList));
  }
  auto manager = MakeManager();
  for (const std::string& id : ids) {
    ASSERT_EQ(FilterListManager::CatalogueResult::kOk,
              manager->SetCatalogueListEnabled(id, true));
  }
  WaitUntilIdle(*manager);

  size_t total = 0;
  size_t refused = 0;
  for (const std::string& id : ids) {
    const FilterListEntry* entry = manager->state().Find(id);
    ASSERT_TRUE(entry);
    total += static_cast<size_t>(entry->bytes);
    if (entry->updated.is_null()) {
      ++refused;
      EXPECT_EQ(ListFailureMessage(ListFailure::kTooLarge), entry->error);
    }
  }
  EXPECT_LE(total, kMaxTotalListBytes);
  EXPECT_EQ(1u, refused);
}

// The lists of the next two tests are on disk and in the state file as an
// earlier run left them.
class OnDiskCapTest : public CatalogueCapTest {
 protected:
  // Seven lists of about 5 MiB each, in this order, with the sizes the state
  // file claims. They are 35 MiB together: the cap holds the first six.
  std::vector<std::string> SeedSevenBigLists(int claimed_bytes) {
    const std::vector<std::string> ids = FirstCatalogueIds(7);
    FilterListState state;
    for (size_t i = 0; i < ids.size(); ++i) {
      EXPECT_EQ(FilterListState::CatalogueAddResult::kAdded,
                state.AddCatalogueList(ids[i]));
      FilterListEntry* entry = state.Find(ids[i]);
      entry->updated = base::Time::FromSecondsSinceUnixEpoch(1700000000);
      entry->bytes = claimed_bytes;
      EXPECT_TRUE(filter_list_storage::WriteList(
          dir_.GetPath(), ids[i],
          SizedList("cat-" + std::to_string(i) + ".example", kBigList)));
    }
    Seed(state);
    return ids;
  }
};

TEST_F(OnDiskCapTest, TheEngineLeavesOutTheListThatTakesTheTotalOverTheCap) {
  // Saved before the size was recorded: every size reads 0.
  const std::vector<std::string> ids = SeedSevenBigLists(0);
  auto manager = MakeManager();
  EXPECT_TRUE(Blocks("cat-0.example"));
  EXPECT_TRUE(Blocks("cat-5.example"));
  EXPECT_FALSE(Blocks("cat-6.example"));
  const FilterListEntry* last = manager->state().Find(ids[6]);
  ASSERT_TRUE(last);
  EXPECT_EQ(ListFailureMessage(ListFailure::kTooLarge), last->error);
  EXPECT_EQ("", manager->state().Find(ids[0])->error);
  // The sizes now on record are the real ones.
  EXPECT_GT(manager->state().Find(ids[0])->bytes, 0);
}

TEST_F(OnDiskCapTest, ASizeForgedInTheStateFileDoesNotHideAList) {
  // The file claims 1 byte for each of seven 5 MiB lists.
  const std::vector<std::string> ids = SeedSevenBigLists(1);
  auto manager = MakeManager();
  EXPECT_TRUE(Blocks("cat-0.example"));
  EXPECT_FALSE(Blocks("cat-6.example"));
  EXPECT_EQ(ListFailureMessage(ListFailure::kTooLarge),
            manager->state().Find(ids[6])->error);
  EXPECT_EQ(static_cast<int>(SizedList("cat-0.example", kBigList).size()),
            manager->state().Find(ids[0])->bytes);

  // Room made: the list is built in on the next rebuild and its error goes.
  ASSERT_EQ(FilterListState::EnableResult::kOk,
            manager->SetListEnabled(ids[0], false));
  WaitUntilIdle(*manager);
  EXPECT_TRUE(Blocks("cat-6.example"));
  EXPECT_EQ("", manager->state().Find(ids[6])->error);
}

TEST_F(CatalogueCapTest, ARefetchOverTheCapKeepsTheGoodCopyAndShowsNoError) {
  // Six lists fill 30 MiB; the seventh holds a 1 MiB copy.
  const std::vector<std::string> ids = FirstCatalogueIds(7);
  FilterListState state;
  for (const std::string& id : ids) {
    ASSERT_EQ(FilterListState::CatalogueAddResult::kAdded,
              state.AddCatalogueList(id));
  }
  for (size_t i = 0; i < 6; ++i) {
    state.Find(ids[i])->bytes = static_cast<int>(kMaxListBytes);
  }
  Seed(state);
  network_.AddResponse(UrlOf(ids[6]),
                       SizedList("cat-old.example", 1024 * 1024));
  auto manager = MakeManager();
  manager->UpdateList(ids[6]);
  WaitUntilIdle(*manager);
  ASSERT_TRUE(Blocks("cat-old.example"));
  const base::Time copy_time = manager->state().Find(ids[6])->updated;
  ASSERT_FALSE(copy_time.is_null());

  // The publisher's list has grown past what fits.
  network_.ClearResponses();
  network_.AddResponse(UrlOf(ids[6]),
                       SizedList("cat-new.example", 3 * 1024 * 1024));
  manager->UpdateList(ids[6]);
  WaitUntilIdle(*manager);
  const FilterListEntry* entry = manager->state().Find(ids[6]);
  EXPECT_TRUE(Blocks("cat-old.example"));
  EXPECT_FALSE(Blocks("cat-new.example"));
  EXPECT_EQ("", entry->error);
  EXPECT_EQ(copy_time, entry->updated);
}

}  // namespace
}  // namespace aerie_adblock
