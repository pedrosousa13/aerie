// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_adblock/browser/filter_engine_service.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "base/run_loop.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "base/task/thread_pool.h"
#include "components/aerie_adblock/browser/adblock_engine.h"
#include "testing/gtest/include/gtest/gtest.h"

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

}  // namespace
}  // namespace aerie_adblock
