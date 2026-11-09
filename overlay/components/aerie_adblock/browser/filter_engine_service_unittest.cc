// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_adblock/browser/filter_engine_service.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/callback_helpers.h"
#include "base/run_loop.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "base/task/thread_pool.h"
#include "components/aerie_adblock/browser/adblock_engine.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace aerie_adblock {
namespace {

std::string SerializedEngine(const char* list) {
  AdblockEngine engine;
  engine.LoadListForTesting(list);
  std::vector<uint8_t> bytes = engine.SerializeForTesting();
  return std::string(bytes.begin(), bytes.end());
}

ClassifiedRequest Script(const char* url, const char* host) {
  return ClassifiedRequest{url, host, "news.test", RequestKind::kScript, true};
}

class FilterEngineServiceTest : public testing::Test {
 protected:
  FilterEngineServiceTest()
      : service_(base::ThreadPool::CreateSequencedTaskRunner({base::MayBlock()}),
                 base::BindLambdaForTesting([this] { ++load_failures_; })) {}

  // Runs the query and waits for the answer.
  bool Ask(const ClassifiedRequest& request) {
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
  int load_failures_ = 0;
  FilterEngineService service_;
};

TEST_F(FilterEngineServiceTest, WithoutStartNothingIsBlocked) {
  EXPECT_FALSE(service_.started());
  EXPECT_FALSE(Ask(Script("https://ads.example/a.js", "ads.example")));
}

// The query is asked right after Start(), before the load can have finished.
// It must wait for the engine and then get the real answer, not "allow".
TEST_F(FilterEngineServiceTest, QueriesAskedBeforeTheLoadFinishesWaitForIt) {
  static const std::string kEngine = SerializedEngine("||ads.example^\n");
  service_.Start(kEngine);
  EXPECT_TRUE(service_.started());
  EXPECT_TRUE(Ask(Script("https://ads.example/a.js", "ads.example")));
  EXPECT_FALSE(Ask(Script("https://news.test/a.js", "news.test")));
}

TEST_F(FilterEngineServiceTest, ManyQueriesAnswerInOrder) {
  static const std::string kEngine = SerializedEngine("||ads.example^\n");
  service_.Start(kEngine);
  std::vector<int> order;
  base::RunLoop loop;
  for (int i = 0; i < 50; ++i) {
    service_.ShouldBlock(
        Script(i % 2 ? "https://ads.example/a.js" : "https://news.test/a.js",
               i % 2 ? "ads.example" : "news.test"),
        base::BindLambdaForTesting([&, i](bool block) {
          EXPECT_EQ(i % 2 == 1, block);
          order.push_back(i);
          if (order.size() == 50) {
            loop.Quit();
          }
        }));
  }
  loop.Run();
  for (int i = 0; i < 50; ++i) {
    EXPECT_EQ(i, order[i]);
  }
}

TEST_F(FilterEngineServiceTest, StartIsOnlyEffectiveOnce) {
  static const std::string kFirst = SerializedEngine("||ads.example^\n");
  static const std::string kSecond = SerializedEngine("||other.example^\n");
  service_.Start(kFirst);
  service_.Start(kSecond);
  EXPECT_TRUE(Ask(Script("https://ads.example/a.js", "ads.example")));
  EXPECT_FALSE(Ask(Script("https://other.example/a.js", "other.example")));
}

// The engine ships inside the build. If it cannot be loaded the build is
// broken, and the service must say so loudly (in the browser: a CHECK) instead
// of quietly blocking nothing. The queries still wait, and are not answered
// from an empty engine before the handler has run.
TEST_F(FilterEngineServiceTest, BadEngineDataIsAFailureNotAnEmptyEngine) {
  static const std::string kJunk = "this is not an engine";
  service_.Start(kJunk);
  base::RunLoop loop;
  service_.ShouldBlock(Script("https://ads.example/a.js", "ads.example"),
                       base::BindLambdaForTesting([&](bool) { loop.Quit(); }));
  loop.Run();
  EXPECT_EQ(1, load_failures_);
}

TEST_F(FilterEngineServiceTest, AnEmptyResourceIsAFailureToo) {
  service_.Start(std::string_view());
  base::RunLoop().RunUntilIdle();
  task_environment_.RunUntilIdle();
  EXPECT_EQ(1, load_failures_);
}

TEST_F(FilterEngineServiceTest, AGoodEngineDoesNotReportAFailure) {
  static const std::string kEngine = SerializedEngine("||ads.example^\n");
  service_.Start(kEngine);
  EXPECT_TRUE(Ask(Script("https://ads.example/a.js", "ads.example")));
  EXPECT_EQ(0, load_failures_);
}

TEST_F(FilterEngineServiceTest, ProcessInstanceIsRegisteredAndCleared) {
  EXPECT_EQ(nullptr, FilterEngineService::Get());
  {
    FilterEngineService owned(
        base::ThreadPool::CreateSequencedTaskRunner({base::MayBlock()}));
    owned.SetAsProcessInstance();
    EXPECT_EQ(&owned, FilterEngineService::Get());
  }
  EXPECT_EQ(nullptr, FilterEngineService::Get());
}

TEST_F(FilterEngineServiceTest, WeakPtrGoesNullWithTheService) {
  base::WeakPtr<FilterEngineService> weak;
  {
    FilterEngineService owned(
        base::ThreadPool::CreateSequencedTaskRunner({base::MayBlock()}));
    weak = owned.GetWeakPtr();
    EXPECT_TRUE(weak);
  }
  EXPECT_FALSE(weak);
}

BuildResult Replace(FilterEngineService& service,
                    std::vector<FilterEngineService::ListInput> lists) {
  BuildResult out;
  base::RunLoop loop;
  service.ReplaceLists(std::move(lists),
                       base::BindLambdaForTesting([&](BuildResult result) {
                         out = result;
                         loop.Quit();
                       }));
  loop.Run();
  return out;
}

TEST_F(FilterEngineServiceTest, ReplaceListsTakesEffectWithoutARestart) {
  static const std::string kEngine = SerializedEngine("||ads.example^\n");
  service_.Start(kEngine);
  EXPECT_FALSE(Ask(Script("https://custom.example/a.js", "custom.example")));

  EXPECT_EQ(ListFailure::kNone,
            Replace(service_, {{"||ads.example^\n", {}}, {"||custom.example^\n", {}}})
                .failure);
  EXPECT_TRUE(Ask(Script("https://custom.example/a.js", "custom.example")));
  EXPECT_TRUE(Ask(Script("https://ads.example/a.js", "ads.example")));
}

TEST_F(FilterEngineServiceTest, AQueryAskedBeforeReplaceListsSeesTheOldRules) {
  static const std::string kEngine = SerializedEngine("||ads.example^\n");
  service_.Start(kEngine);
  bool before = false;
  base::RunLoop loop;
  service_.ShouldBlock(Script("https://ads.example/a.js", "ads.example"),
                       base::BindLambdaForTesting([&](bool block) {
                         before = block;
                         loop.Quit();
                       }));
  service_.ReplaceLists({{"||other.example^\n", {}}}, base::DoNothing());
  loop.Run();
  EXPECT_TRUE(before);
  EXPECT_FALSE(Ask(Script("https://ads.example/a.js", "ads.example")));
}

TEST_F(FilterEngineServiceTest, ABadListLeavesTheWorkingRulesAlone) {
  static const std::string kEngine = SerializedEngine("||ads.example^\n");
  service_.Start(kEngine);
  BuildResult result = Replace(
      service_, {{"||new.example^\n", {}}, {"<html>not a list</html>", {}}});
  EXPECT_EQ(ListFailure::kLooksLikeHtml, result.failure);
  EXPECT_EQ(1u, result.failed_index);
  EXPECT_TRUE(Ask(Script("https://ads.example/a.js", "ads.example")));
  EXPECT_FALSE(Ask(Script("https://new.example/a.js", "new.example")));
}

TEST_F(FilterEngineServiceTest, ListsCanComeFromFilesAndAMissingFileFails) {
  static const std::string kEngine = SerializedEngine("||ads.example^\n");
  service_.Start(kEngine);
  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  base::FilePath file = dir.GetPath().AppendASCII("l.txt");
  ASSERT_TRUE(base::WriteFile(file, "||fromfile.example^\n"));

  EXPECT_EQ(ListFailure::kNone, Replace(service_, {{"", file}}).failure);
  EXPECT_TRUE(Ask(Script("https://fromfile.example/a.js", "fromfile.example")));

  BuildResult missing = Replace(service_, {{"", dir.GetPath().AppendASCII("gone.txt")}});
  EXPECT_EQ(ListFailure::kUnreadable, missing.failure);
  EXPECT_TRUE(Ask(Script("https://fromfile.example/a.js", "fromfile.example")));
}

TEST_F(FilterEngineServiceTest, ASiteExceptionAllowsThatSiteAndItsSubdomainsOnly) {
  static const std::string kEngine = SerializedEngine("||ads.example^\n");
  service_.Start(kEngine);
  service_.SetExceptions({"news.test"});

  auto ask_on = [&](const char* top_site) {
    ClassifiedRequest request = Script("https://ads.example/a.js", "ads.example");
    request.source_hostname = top_site;
    return Ask(request);
  };
  EXPECT_FALSE(ask_on("news.test"));
  EXPECT_FALSE(ask_on("www.news.test"));
  EXPECT_TRUE(ask_on("other.test"));
  EXPECT_TRUE(ask_on("notnews.test"));

  service_.SetExceptions({});  // Takes effect at once, no restart.
  EXPECT_TRUE(ask_on("news.test"));
}

TEST_F(FilterEngineServiceTest, QueriesWaitWhileTheSettingsLoadThenSeeTheExceptions) {
  static const std::string kEngine = SerializedEngine("||ads.example^\n");
  service_.Start(kEngine);
  service_.HoldRequestsUntilSettingsLoaded();
  std::vector<bool> answers;
  service_.ShouldBlock(Script("https://ads.example/a.js", "ads.example"),
                       base::BindLambdaForTesting([&](bool b) { answers.push_back(b); }));
  base::RunLoop().RunUntilIdle();
  task_environment_.RunUntilIdle();
  EXPECT_TRUE(answers.empty());  // Held, not answered "allow".

  service_.SetExceptions({"news.test"});  // The query's top site is news.test.
  EXPECT_EQ(std::vector<bool>{false}, answers);
}

TEST_F(FilterEngineServiceTest, ReleasingTheHoldAnswersWithTheRules) {
  static const std::string kEngine = SerializedEngine("||ads.example^\n");
  service_.Start(kEngine);
  service_.HoldRequestsUntilSettingsLoaded();
  bool blocked = false;
  base::RunLoop loop;
  service_.ShouldBlock(Script("https://ads.example/a.js", "ads.example"),
                       base::BindLambdaForTesting([&](bool b) {
                         blocked = b;
                         loop.Quit();
                       }));
  service_.ReleaseHeldRequests();
  loop.Run();
  EXPECT_TRUE(blocked);
}

TEST_F(FilterEngineServiceTest, RecordBlockedReachesTheCallback) {
  int calls = 0;
  content::GlobalRenderFrameHostId seen;
  service_.RecordBlocked({1, 2});  // No callback yet: ignored.
  service_.SetBlockedRequestCallback(base::BindLambdaForTesting(
      [&](content::GlobalRenderFrameHostId id) {
        ++calls;
        seen = id;
      }));
  service_.RecordBlocked({3, 4});
  EXPECT_EQ(1, calls);
  EXPECT_EQ(content::GlobalRenderFrameHostId(3, 4), seen);
}

TEST_F(FilterEngineServiceTest, ASessionExceptionAppliesToThatSessionOnly) {
  static const std::string kEngine = SerializedEngine("||ads.example^\n");
  service_.Start(kEngine);
  int id_a = 0, id_b = 0;
  const SessionKey session_a = SessionKey::OffTheRecord(&id_a);
  const SessionKey session_b = SessionKey::OffTheRecord(&id_b);
  service_.SetSessionExceptions(session_a, {"news.test"});

  auto ask = [&](SessionKey session) {
    ClassifiedRequest request = Script("https://ads.example/a.js", "ads.example");
    request.source_hostname = "news.test";
    request.session = session;
    return Ask(request);
  };
  EXPECT_FALSE(ask(session_a));   // The session that set it: ads load.
  EXPECT_TRUE(ask(session_b));    // Another incognito session.
  EXPECT_TRUE(ask(SessionKey::Regular()));  // The regular profile.
  EXPECT_TRUE(service_.exceptions().empty());  // Nothing persistable changed.

  service_.SetSessionExceptions(session_a, {});  // The session ended.
  EXPECT_TRUE(ask(session_a));
}

TEST_F(FilterEngineServiceTest, TheRegularSessionIsNotAnOffTheRecordSession) {
  static const std::string kEngine = SerializedEngine("||ads.example^\n");
  service_.Start(kEngine);
  // A regular key can never hold a session exception.
  service_.SetSessionExceptions(SessionKey::Regular(), {"news.test"});
  EXPECT_FALSE(service_.IsExcepted("news.test", SessionKey::Regular()));
  ClassifiedRequest request = Script("https://ads.example/a.js", "ads.example");
  request.source_hostname = "news.test";
  EXPECT_EQ(SessionKey::Regular(), request.session);  // Not set: regular.
  EXPECT_TRUE(Ask(request));
}

}  // namespace

// ---- Cosmetic filtering (issue #9) ----

class CosmeticServiceTest : public FilterEngineServiceTest {
 protected:
  std::string Page(const char* top,
                   const char* page,
                   SessionKey session = SessionKey::Regular()) {
    std::string css;
    base::RunLoop loop;
    service_.GetCosmeticStylesheet(
        GURL(top), GURL(page),
        base::BindLambdaForTesting([&](std::string result) {
          css = std::move(result);
          loop.Quit();
        }),
        session);
    loop.Run();
    return css;
  }

  std::string Generic(const char* top,
                      const char* page,
                      const char* classes,
                      const char* ids,
                      SessionKey session = SessionKey::Regular()) {
    std::string css;
    base::RunLoop loop;
    service_.GetGenericStylesheet(
        GURL(top), GURL(page), classes, ids,
        base::BindLambdaForTesting([&](std::string result) {
          css = std::move(result);
          loop.Quit();
        }),
        session);
    loop.Run();
    return css;
  }
};

TEST_F(CosmeticServiceTest, WithoutStartTheStylesheetIsEmpty) {
  EXPECT_TRUE(Page("https://news.test/", "https://news.test/").empty());
}

TEST_F(CosmeticServiceTest, StylesheetsWaitForTheEngineAndHideTheRules) {
  static const std::string kEngine =
      SerializedEngine("news.test##.site-ad\n##.ad-banner\n");
  service_.Start(kEngine);
  EXPECT_NE(Page("https://news.test/", "https://news.test/")
                .find(".site-ad{display:none!important}"),
            std::string::npos);
  EXPECT_EQ(Generic("https://news.test/", "https://news.test/",
                    "ad-banner x", ""),
            ".ad-banner{display:none!important}\n");
}

TEST_F(CosmeticServiceTest, OnlyWebPagesGetAStylesheet) {
  static const std::string kEngine = SerializedEngine("##.ad-banner\n");
  service_.Start(kEngine);
  EXPECT_TRUE(Generic("chrome://settings/", "chrome://settings/", "ad-banner", "")
                  .empty());
  EXPECT_TRUE(Generic("https://news.test/", "about:blank", "ad-banner", "")
                  .empty());
}

TEST_F(CosmeticServiceTest, ThePolicyCanTurnCosmeticFilteringOffPerSite) {
  static const std::string kEngine = SerializedEngine("##.ad-banner\n");
  service_.Start(kEngine);
  EXPECT_TRUE(service_.IsCosmeticFilteringEnabled(GURL("https://news.test/"), SessionKey::Regular()));
  service_.SetCosmeticFilteringPolicy(
      base::BindRepeating([](const GURL& top_level_url, SessionKey) {
        return top_level_url.host() != "off.test";
      }));
  EXPECT_FALSE(service_.IsCosmeticFilteringEnabled(GURL("https://off.test/"), SessionKey::Regular()));
  EXPECT_TRUE(Generic("https://off.test/", "https://off.test/", "ad-banner", "")
                  .empty());
  // An ad iframe on the page is judged by the page's switch, not its own.
  EXPECT_TRUE(Generic("https://off.test/", "https://news.test/", "ad-banner", "")
                  .empty());
  EXPECT_FALSE(Generic("https://news.test/", "https://news.test/", "ad-banner", "")
                   .empty());
}

TEST_F(CosmeticServiceTest, ASiteWithBlockingOffGetsNoHideSheet) {
  static const std::string kEngine = SerializedEngine("##.ad-banner\n");
  service_.Start(kEngine);
  service_.SetExceptions({"off.test"});
  EXPECT_FALSE(service_.IsCosmeticFilteringEnabled(GURL("https://off.test/"), SessionKey::Regular()));
  EXPECT_FALSE(service_.IsCosmeticFilteringEnabled(GURL("https://www.off.test/"), SessionKey::Regular()));
  EXPECT_TRUE(service_.IsCosmeticFilteringEnabled(GURL("https://news.test/"), SessionKey::Regular()));
  EXPECT_TRUE(Page("https://off.test/", "https://off.test/").empty());
  EXPECT_TRUE(Generic("https://off.test/", "https://off.test/", "ad-banner", "")
                  .empty());
  // An ad iframe on the excepted page follows the page.
  EXPECT_TRUE(Generic("https://off.test/", "https://ads.test/", "ad-banner", "")
                  .empty());
  EXPECT_FALSE(Generic("https://news.test/", "https://news.test/", "ad-banner", "")
                   .empty());
  service_.SetExceptions({});  // Blocking back on: hiding is back.
  EXPECT_FALSE(Generic("https://off.test/", "https://off.test/", "ad-banner", "")
                   .empty());
}

TEST_F(CosmeticServiceTest, ASessionExceptionStopsHidingInThatSessionOnly) {
  static const std::string kEngine = SerializedEngine("##.ad-banner\n");
  service_.Start(kEngine);
  int id = 0;
  const SessionKey incognito = SessionKey::OffTheRecord(&id);
  service_.SetSessionExceptions(incognito, {"off.test"});
  EXPECT_TRUE(Generic("https://off.test/", "https://off.test/", "ad-banner", "",
                      incognito)
                  .empty());
  EXPECT_FALSE(Generic("https://off.test/", "https://off.test/", "ad-banner", "")
                   .empty());
}

TEST_F(CosmeticServiceTest, StylesheetsWaitWhileTheSettingsLoad) {
  static const std::string kEngine = SerializedEngine("##.ad-banner\n");
  service_.Start(kEngine);
  service_.HoldRequestsUntilSettingsLoaded();
  std::vector<std::string> answers;
  service_.GetGenericStylesheet(
      GURL("https://off.test/"), GURL("https://off.test/"), "ad-banner", "",
      base::BindLambdaForTesting(
          [&](std::string css) { answers.push_back(std::move(css)); }),
      SessionKey::Regular());
  task_environment_.RunUntilIdle();
  EXPECT_TRUE(answers.empty());  // Held, so the exception can still apply.
  service_.SetExceptions({"off.test"});
  task_environment_.RunUntilIdle();
  ASSERT_EQ(1u, answers.size());
  EXPECT_TRUE(answers[0].empty());
}

}  // namespace aerie_adblock
