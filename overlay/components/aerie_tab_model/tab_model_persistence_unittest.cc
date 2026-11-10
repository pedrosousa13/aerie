// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_tab_model/tab_model_persistence.h"

#include <optional>
#include <string>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/strings/string_number_conversions.h"
#include "base/task/thread_pool.h"
#include "base/test/simple_test_clock.h"
#include "base/test/task_environment.h"
#include "base/test/test_future.h"
#include "components/aerie_tab_model/tab_model.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace aerie_tab_model {

namespace {

class TabModelPersistenceTest : public testing::Test {
 protected:
  TabModelPersistenceTest() {
    EXPECT_TRUE(dir_.CreateUniqueTempDir());
    clock_.SetNow(base::Time::FromMillisecondsSinceUnixEpoch(1'700'000'000'000));
    path_ = dir_.GetPath().AppendASCII(kStateDirectory).AppendASCII(kStateFile);
  }

  scoped_refptr<base::SequencedTaskRunner> io_runner() {
    return base::ThreadPool::CreateSequencedTaskRunner(
        {base::MayBlock(), base::TaskShutdownBehavior::BLOCK_SHUTDOWN});
  }

  // One session: a model that reads path_, runs `body`, flushes and goes away.
  template <typename Body>
  LoadStatus Session(Body body) {
    TabModel model(&clock_, base::BindRepeating([](int* n) { return "id-" + base::NumberToString((*n)++); }, &next_id_));
    TabModelPersistence persistence(&model, path_, io_runner());
    base::test::TestFuture<LoadStatus> loaded;
    persistence.Load(loaded.GetCallback());
    LoadStatus status = loaded.Get();
    body(model);
    persistence.FlushPendingWrite();
    task_environment_.RunUntilIdle();
    return status;
  }

  base::FilePath BadPath() const { return base::FilePath(path_.value() + kBadSuffix); }

  void WriteRaw(const std::string& text) {
    ASSERT_TRUE(base::CreateDirectory(path_.DirName()));
    ASSERT_TRUE(base::WriteFile(path_, text));
  }

  base::test::TaskEnvironment task_environment_;
  base::ScopedTempDir dir_;
  base::SimpleTestClock clock_;
  base::FilePath path_;
  int next_id_ = 1;
};

TEST_F(TabModelPersistenceTest, AFreshProfileGetsItsWorkspaceAndKeepsItAcrossARestart) {
  EXPECT_EQ(Session([](TabModel& model) {
              EXPECT_EQ(model.workspace().name, "Personal");
              model.UpdateWorkspace([](Workspace& w) {
                w.colour = "#336699";
                w.theme = "oled";
                w.mode = "dark";
              });
            }),
            LoadStatus::kMissing);
  EXPECT_TRUE(base::PathExists(path_));

  EXPECT_EQ(Session([](TabModel& model) {
              EXPECT_EQ(model.workspace().id, kDefaultWorkspaceId);
              EXPECT_EQ(model.workspace().name, "Personal");
              EXPECT_EQ(model.workspace().colour, "#336699");
              EXPECT_EQ(model.workspace().theme, "oled");
              EXPECT_EQ(model.workspace().mode, "dark");
            }),
            LoadStatus::kOk);
}

TEST_F(TabModelPersistenceTest, TabsAndFoldersSurviveARestartAsStored) {
  std::string pinned_id;
  std::string folder_id;
  Session([&](TabModel& model) {
    FolderRecord folder;
    folder.name = "Reading";
    folder_id = model.AddFolder(std::move(folder));
    TabRecord tab;
    tab.section = Section::kPinned;
    tab.parent = folder_id;
    tab.pinned_url = "https://example.com/";
    tab.manual_name = "Example";
    pinned_id = model.AddTab(std::move(tab));
  });
  Session([&](TabModel& model) {
    const TabRecord* tab = model.GetTab(pinned_id);
    ASSERT_TRUE(tab);
    EXPECT_EQ(tab->pinned_url, "https://example.com/");
    EXPECT_EQ(tab->manual_name, "Example");
    EXPECT_EQ(tab->parent, folder_id);
    ASSERT_TRUE(model.GetFolder(folder_id));
    EXPECT_EQ(model.GetFolder(folder_id)->name, "Reading");
    EXPECT_GE(model.change_log().size(), 3u);
  });
}

TEST_F(TabModelPersistenceTest, ALiveTodayTabIsNotKeptButASleepingOneIs) {
  std::string live;
  std::string asleep;
  Session([&](TabModel& model) {
    live = model.AddTab(TabRecord());
    TabRecord tab;
    tab.sleeping = true;
    asleep = model.AddTab(std::move(tab));
  });
  Session([&](TabModel& model) {
    EXPECT_EQ(model.GetTab(live), nullptr);
    EXPECT_TRUE(model.GetTab(asleep));
  });
}

TEST_F(TabModelPersistenceTest, CountersKeepGrowingAcrossRestarts) {
  int64_t after_first = 0;
  Session([&](TabModel& model) {
    model.AddTab(TabRecord());
    after_first = model.next_change();
  });
  Session([&](TabModel& model) {
    EXPECT_GE(model.next_change(), after_first);
    int64_t previous = 0;
    for (const ChangeRecord& change : model.change_log()) {
      EXPECT_GT(change.counter, previous);
      previous = change.counter;
    }
  });
}

TEST_F(TabModelPersistenceTest, ACorruptFileStartsEmptyAndIsKept) {
  WriteRaw("{ this is not json");
  EXPECT_EQ(Session([](TabModel& model) {
              EXPECT_TRUE(model.tabs().empty());
              EXPECT_EQ(model.workspace().name, "Personal");
            }),
            LoadStatus::kCorrupt);
  std::string bad;
  ASSERT_TRUE(base::ReadFileToString(BadPath(), &bad));
  EXPECT_EQ(bad, "{ this is not json");
  // A fresh, valid file took its place.
  std::string now;
  ASSERT_TRUE(base::ReadFileToString(path_, &now));
  EXPECT_TRUE(base::JSONReader::Read(now, base::JSON_PARSE_RFC));
}

TEST_F(TabModelPersistenceTest, AJsonFileOfTheWrongShapeIsKept) {
  WriteRaw("[1, 2, 3]");
  EXPECT_EQ(Session([](TabModel&) {}), LoadStatus::kCorrupt);
  std::string bad;
  ASSERT_TRUE(base::ReadFileToString(BadPath(), &bad));
  EXPECT_EQ(bad, "[1, 2, 3]");
}

TEST_F(TabModelPersistenceTest, ANewerSchemaStartsEmptyAndIsKept) {
  WriteRaw(R"({"version": 99, "anything": true})");
  EXPECT_EQ(Session([](TabModel& model) { EXPECT_TRUE(model.tabs().empty()); }),
            LoadStatus::kNewerSchema);
  std::string bad;
  ASSERT_TRUE(base::ReadFileToString(BadPath(), &bad));
  EXPECT_EQ(bad, R"({"version": 99, "anything": true})");
}

TEST_F(TabModelPersistenceTest, ANewBadFileReplacesAnOlderOne) {
  WriteRaw("first");
  Session([](TabModel&) {});
  WriteRaw("second");
  Session([](TabModel&) {});
  std::string bad;
  ASSERT_TRUE(base::ReadFileToString(BadPath(), &bad));
  EXPECT_EQ(bad, "second");
}

TEST_F(TabModelPersistenceTest, NothingIsWrittenBeforeTheReadFinishes) {
  TabModel model(&clock_);
  TabModelPersistence persistence(&model, path_, io_runner());
  persistence.Load();
  // The read has not run yet. A change now must not create the file.
  model.AddTab(TabRecord());
  persistence.FlushPendingWrite();
  EXPECT_FALSE(persistence.loaded());
  EXPECT_FALSE(base::PathExists(path_));
  task_environment_.RunUntilIdle();
  EXPECT_TRUE(persistence.loaded());
}

TEST_F(TabModelPersistenceTest, ATabOpenedWhileReadingIsKept) {
  Session([](TabModel& model) { model.AddTab(TabRecord()); });
  TabModel model(&clock_);
  TabModelPersistence persistence(&model, path_, io_runner());
  persistence.Load();
  std::string early = model.AddTab(TabRecord());
  task_environment_.RunUntilIdle();
  ASSERT_TRUE(persistence.loaded());
  EXPECT_TRUE(model.GetTab(early));
  persistence.FlushPendingWrite();
  task_environment_.RunUntilIdle();
}

TEST_F(TabModelPersistenceTest, WithoutAPathNothingTouchesTheDisk) {
  TabModel model(&clock_);
  TabModelPersistence persistence(&model, base::FilePath(), nullptr);
  base::test::TestFuture<LoadStatus> loaded;
  persistence.Load(loaded.GetCallback());
  EXPECT_EQ(loaded.Get(), LoadStatus::kMissing);
  model.AddTab(TabRecord());
  persistence.FlushPendingWrite();
  EXPECT_FALSE(base::PathExists(path_));
}

TEST_F(TabModelPersistenceTest, DestroyingTheServiceFlushesTheLastChange) {
  TabModel model(&clock_);
  {
    TabModelPersistence persistence(&model, path_, io_runner());
    base::test::TestFuture<LoadStatus> loaded;
    persistence.Load(loaded.GetCallback());
    loaded.Get();
    model.AddTab(TabRecord());
    // Destruction flushes: closing the profile must not lose the last change.
  }
  task_environment_.RunUntilIdle();
  std::string text;
  ASSERT_TRUE(base::ReadFileToString(path_, &text));
  std::optional<base::Value> json = base::JSONReader::Read(text, base::JSON_PARSE_RFC);
  ASSERT_TRUE(json && json->is_dict());
  EXPECT_EQ(json->GetDict().FindList("tabs")->size(), 1u);
}

}  // namespace

}  // namespace aerie_tab_model
