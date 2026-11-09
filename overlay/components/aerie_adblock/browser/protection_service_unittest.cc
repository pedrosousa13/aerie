// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_adblock/browser/protection_service.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/run_loop.h"
#include "base/task/thread_pool.h"
#include "base/test/bind.h"
#include "base/test/metrics/histogram_tester.h"
#include "base/test/task_environment.h"
#include "components/aerie_adblock/browser/protection_engine.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace aerie_adblock {
namespace {

std::string SerializedEngine(const char* list) {
  ProtectionEngine engine;
  engine.LoadListForTesting(list);
  std::vector<uint8_t> bytes = engine.SerializeForTesting();
  return std::string(bytes.begin(), bytes.end());
}

class ProtectionServiceTest : public testing::Test {
 protected:
  ProtectionServiceTest()
      : service_(base::ThreadPool::CreateSequencedTaskRunner({base::MayBlock()}),
                 base::BindLambdaForTesting([this] { ++load_failures_; }),
                 /*min_entries=*/1) {}

  bool Ask(const std::string& host) {
    bool answer = false;
    base::RunLoop loop;
    service_.IsListed(host, base::BindLambdaForTesting([&](bool listed) {
                        answer = listed;
                        loop.Quit();
                      }));
    loop.Run();
    return answer;
  }

  base::test::TaskEnvironment task_environment_;
  base::HistogramTester histograms_;
  int load_failures_ = 0;
  ProtectionService service_;
};

TEST_F(ProtectionServiceTest, WithoutStartNothingIsListed) {
  EXPECT_FALSE(service_.started());
  EXPECT_FALSE(Ask("evil.example"));
}

// Asked right after Start(), before the load can have finished: the query must
// wait for the engine and get the real answer, never "not listed".
TEST_F(ProtectionServiceTest, QueriesAskedBeforeTheLoadFinishesWaitForIt) {
  static const std::string kEngine = SerializedEngine("evil.example\n");
  service_.Start(kEngine);
  EXPECT_TRUE(Ask("evil.example"));
  EXPECT_TRUE(Ask("login.evil.example"));
  EXPECT_FALSE(Ask("good.example"));
  histograms_.ExpectUniqueSample("Aerie.Protection.ListLoadSuccess", true, 1);
}

TEST_F(ProtectionServiceTest, StartIsOnlyEffectiveOnce) {
  static const std::string kFirst = SerializedEngine("evil.example\n");
  static const std::string kSecond = SerializedEngine("other.example\n");
  service_.Start(kFirst);
  service_.Start(kSecond);
  EXPECT_TRUE(Ask("evil.example"));
  EXPECT_FALSE(Ask("other.example"));
}

TEST_F(ProtectionServiceTest, BadEngineDataIsAFailureNotAnEmptyEngine) {
  static const std::string kJunk = "this is not an engine";
  service_.Start(kJunk);
  Ask("evil.example");
  EXPECT_EQ(1, load_failures_);
  histograms_.ExpectUniqueSample("Aerie.Protection.ListLoadSuccess", false, 1);
}

TEST_F(ProtectionServiceTest, AnEmptyResourceIsAFailureToo) {
  service_.Start(std::string_view());
  task_environment_.RunUntilIdle();
  EXPECT_EQ(1, load_failures_);
}

// The service is built with the shipped floor here: a list of one domain is an
// empty or truncated download, and must fail loudly.
TEST_F(ProtectionServiceTest, AListUnderTheFloorIsALoadFailure) {
  static const std::string kTiny = SerializedEngine("evil.example\n");
  int failures = 0;
  ProtectionService strict(
      base::ThreadPool::CreateSequencedTaskRunner({base::MayBlock()}),
      base::BindLambdaForTesting([&failures] { ++failures; }));
  strict.Start(kTiny);
  task_environment_.RunUntilIdle();
  EXPECT_EQ(1, failures);
  histograms_.ExpectUniqueSample("Aerie.Protection.ListLoadSuccess", false, 1);
}

TEST_F(ProtectionServiceTest, AListAtTheFloorLoads) {
  static const std::string kTwo = SerializedEngine("a.example\nb.example\n");
  int failures = 0;
  ProtectionService two(
      base::ThreadPool::CreateSequencedTaskRunner({base::MayBlock()}),
      base::BindLambdaForTesting([&failures] { ++failures; }),
      /*min_entries=*/2);
  two.Start(kTwo);
  task_environment_.RunUntilIdle();
  EXPECT_EQ(0, failures);
}

TEST_F(ProtectionServiceTest, TheTestListOnlyAddsDomains) {
  static const std::string kEngine = SerializedEngine("evil.example\n");
  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  base::FilePath extra = dir.GetPath().AppendASCII("test_list.txt");
  ASSERT_TRUE(base::WriteFile(extra, "phish.bench.test\n"));
  service_.Start(kEngine, extra);
  EXPECT_TRUE(Ask("phish.bench.test"));
  EXPECT_TRUE(Ask("evil.example"));  // the shipped list still applies
  EXPECT_FALSE(Ask("good.example"));
  EXPECT_EQ(0, load_failures_);
}

TEST_F(ProtectionServiceTest, AnUnreadableTestListIsALoadFailure) {
  static const std::string kEngine = SerializedEngine("evil.example\n");
  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  service_.Start(kEngine, dir.GetPath().AppendASCII("missing.txt"));
  Ask("evil.example");
  EXPECT_EQ(1, load_failures_);
}

TEST_F(ProtectionServiceTest, ProcessInstanceIsRegisteredAndCleared) {
  EXPECT_EQ(nullptr, ProtectionService::Get());
  {
    ProtectionService owned(
        base::ThreadPool::CreateSequencedTaskRunner({base::MayBlock()}));
    owned.SetAsProcessInstance();
    EXPECT_EQ(&owned, ProtectionService::Get());
  }
  EXPECT_EQ(nullptr, ProtectionService::Get());
}

}  // namespace
}  // namespace aerie_adblock
