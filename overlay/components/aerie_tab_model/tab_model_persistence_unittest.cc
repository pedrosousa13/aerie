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
#include "components/aerie_tab_model/folder_tree.h"
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

TEST_F(TabModelPersistenceTest, FolderOrderExpandStateAndPinnedContentsSurviveARestart) {
  std::string open_folder;
  std::string shut_folder;
  std::string inner;
  std::string pinned_tab;
  std::string today_tab;
  std::string today_folder;
  Session([&](TabModel& model) {
    open_folder = CreateFolder(model, Section::kPinned, std::string(), "Open");
    shut_folder = CreateFolder(model, Section::kPinned, std::string(), "Shut");
    inner = CreateFolder(model, Section::kPinned, open_folder, "Inner");
    SetFolderExpanded(model, shut_folder, false);
    TabRecord tab;
    tab.section = Section::kPinned;
    tab.parent = inner;
    tab.pinned_url = "https://example.com/";
    pinned_tab = model.AddTab(std::move(tab));
    TabRecord today;
    today.section = Section::kToday;
    today.last_active = clock_.Now();
    today_tab = model.AddTab(std::move(today));
    today_folder = CreateFolder(model, Section::kToday, std::string(), "T");
    model.MoveTab(today_tab, Section::kToday, today_folder, 0);
  });
  Session([&](TabModel& model) {
    ASSERT_TRUE(model.GetFolder(open_folder));
    EXPECT_TRUE(model.GetFolder(open_folder)->expanded);
    EXPECT_FALSE(model.GetFolder(shut_folder)->expanded);
    EXPECT_EQ(model.GetTab(pinned_tab)->parent, inner);
    EXPECT_EQ(model.GetFolder(inner)->parent, open_folder);
    // Shut was made last, so it is first: the order is kept.
    const std::vector<TreeRow> rows = BuildVisibleRows(
        model, Section::kPinned, [](const std::string&) { return true; });
    ASSERT_EQ(rows.size(), 4u);
    EXPECT_EQ(rows[0].id, shut_folder);
    EXPECT_EQ(rows[1].id, open_folder);
    // A live Today tab that no restored tab claims is dropped when the restore
    // pass ends (issue #223); the folder that held it stays (empty).
    EXPECT_TRUE(model.IsAwaitingRestore(today_tab));
    model.RemoveUnclaimedRestoredTabs();
    EXPECT_EQ(model.GetTab(today_tab), nullptr);
    EXPECT_TRUE(model.GetFolder(today_folder));
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
    model.RemoveUnclaimedRestoredTabs();
    EXPECT_EQ(model.GetTab(live), nullptr);
    EXPECT_TRUE(model.GetTab(asleep));
  });
}

// Issue #223: a restored tab that carries the record's id gets the record back,
// with its name, folder and place in the folder.
TEST_F(TabModelPersistenceTest, AClaimedTodayTabKeepsItsRecordAcrossARestart) {
  std::string folder;
  std::string named;
  std::string plain;
  Session([&](TabModel& model) {
    folder = CreateFolder(model, Section::kToday, std::string(), "Work");
    TabRecord first;
    first.manual_name = "Kept name";
    first.last_active = clock_.Now();
    named = model.AddTab(std::move(first));
    ASSERT_TRUE(model.MoveTab(named, Section::kToday, folder, 2));
    plain = model.AddTab(TabRecord());
  });
  Session([&](TabModel& model) {
    // Only `named` is claimed by a restored tab; `plain` is not.
    EXPECT_EQ(model.ClaimRestoredTab(named), named);
    EXPECT_EQ(model.RemoveUnclaimedRestoredTabs(), 1u);
    EXPECT_EQ(model.GetTab(plain), nullptr);
    const TabRecord* tab = model.GetTab(named);
    ASSERT_TRUE(tab);
    EXPECT_EQ(tab->manual_name, "Kept name");
    EXPECT_EQ(tab->parent, folder);
    EXPECT_EQ(tab->position, 2);
    EXPECT_TRUE(model.GetFolder(folder));
  });
  // And it is still there for the restart after that.
  Session([&](TabModel& model) {
    EXPECT_EQ(model.ClaimRestoredTab(named), named);
    EXPECT_EQ(model.GetTab(named)->manual_name, "Kept name");
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
  EXPECT_FALSE(persistence.persists_to_disk());
  model.AddTab(TabRecord());
  persistence.FlushPendingWrite();
  task_environment_.RunUntilIdle();
  EXPECT_FALSE(base::PathExists(path_));
  EXPECT_TRUE(base::IsDirectoryEmpty(dir_.GetPath()));
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
