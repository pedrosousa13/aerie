// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_tab_model/archive_schedule.h"

#include <set>
#include <string>

#include "base/functional/bind.h"
#include "base/test/task_environment.h"
#include "components/aerie_tab_model/archive.h"
#include "components/aerie_tab_model/folder_tree.h"
#include "components/aerie_tab_model/tab_model.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace aerie_tab_model {

namespace {

class ArchiveScheduleTest : public testing::Test {
 protected:
  ArchiveScheduleTest()
      : model_(task_environment_.GetMockClock()),
        schedule_(&model_,
                  task_environment_.GetMockClock(),
                  task_environment_.GetMockTickClock(),
                  base::BindRepeating(&ArchiveScheduleTest::Archive,
                                      base::Unretained(this)),
                  base::BindRepeating(&ArchiveScheduleTest::IsExempt,
                                      base::Unretained(this))) {}

  base::Time Now() { return task_environment_.GetMockClock()->Now(); }

  std::string Add(Section section, const std::string& parent = std::string()) {
    TabRecord tab;
    tab.section = section;
    tab.parent = parent;
    tab.last_active = Now();
    return model_.AddTab(std::move(tab));
  }

  // The owner closes the tab; here it only stamps the record.
  bool Archive(const std::string& id) {
    archive_calls_.push_back(id);
    if (refuse_.contains(id)) {
      return false;
    }
    ArchiveData data;
    data.url = "https://example.com/";
    return ArchiveTab(model_, id, std::move(data), Now());
  }

  bool IsExempt(const std::string& id) {
    ++exempt_calls_;
    return exempt_.contains(id);
  }

  bool IsArchived(const std::string& id) {
    return model_.GetTab(id) && model_.GetTab(id)->archived_at;
  }

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  TabModel model_;
  ArchiveSchedule schedule_;
  std::set<std::string> exempt_;
  std::set<std::string> refuse_;
  std::vector<std::string> archive_calls_;
  int exempt_calls_ = 0;
};

TEST_F(ArchiveScheduleTest, ATodayTabIsArchivedAtTheTimeoutAndNotBefore) {
  schedule_.SetArchiveAfter(base::Hours(12));
  const std::string id = Add(Section::kToday);
  schedule_.Rearm();
  task_environment_.FastForwardBy(base::Hours(11) + base::Minutes(59));
  EXPECT_FALSE(IsArchived(id));
  task_environment_.FastForwardBy(base::Minutes(1));
  EXPECT_TRUE(IsArchived(id));
}

TEST_F(ArchiveScheduleTest, TheSettingChangesTheMomentWithoutARestart) {
  schedule_.SetArchiveAfter(base::Hours(12));
  const std::string id = Add(Section::kToday);
  schedule_.Rearm();
  schedule_.SetArchiveAfter(base::Hours(24));
  task_environment_.FastForwardBy(base::Hours(13));
  EXPECT_FALSE(IsArchived(id));
  task_environment_.FastForwardBy(base::Hours(11));
  EXPECT_TRUE(IsArchived(id));
}

TEST_F(ArchiveScheduleTest, SevenDaysAndNeverFollowTheSetting) {
  const std::string id = Add(Section::kToday);
  schedule_.SetArchiveAfter(base::Days(7));
  task_environment_.FastForwardBy(base::Days(6));
  EXPECT_FALSE(IsArchived(id));
  schedule_.SetArchiveAfter(std::nullopt);
  EXPECT_FALSE(schedule_.timer_pending());
  task_environment_.FastForwardBy(base::Days(30));
  EXPECT_FALSE(IsArchived(id));
  // Back to 7 days: the tab has been idle longer than that, so it goes now.
  schedule_.SetArchiveAfter(base::Days(7));
  task_environment_.FastForwardBy(base::Seconds(1));
  EXPECT_TRUE(IsArchived(id));
}

TEST_F(ArchiveScheduleTest, PinnedFavoritesAndPinnedFolderTabsNeverArchive) {
  schedule_.SetArchiveAfter(base::Hours(12));
  const std::string pinned = Add(Section::kPinned);
  const std::string favorite = Add(Section::kFavorite);
  const std::string pinned_folder =
      CreateFolder(model_, Section::kPinned, std::string(), "P");
  const std::string in_pinned_folder = Add(Section::kPinned, pinned_folder);
  const std::string today_folder =
      CreateFolder(model_, Section::kToday, std::string(), "T");
  const std::string in_today_folder = Add(Section::kToday, today_folder);
  schedule_.Rearm();
  task_environment_.FastForwardBy(base::Days(2));
  EXPECT_FALSE(IsArchived(pinned));
  EXPECT_FALSE(IsArchived(favorite));
  EXPECT_FALSE(IsArchived(in_pinned_folder));
  EXPECT_TRUE(IsArchived(in_today_folder));
}

TEST_F(ArchiveScheduleTest, TheSelectedOrAudibleTabIsLeftAloneUntilItIsFree) {
  schedule_.SetArchiveAfter(base::Hours(12));
  const std::string id = Add(Section::kToday);
  exempt_.insert(id);
  schedule_.Rearm();
  EXPECT_FALSE(schedule_.timer_pending());
  task_environment_.FastForwardBy(base::Days(1));
  EXPECT_FALSE(IsArchived(id));
  // The exemption ends; the owner says so and the tab goes at once, as its last
  // use is older than the timeout.
  exempt_.clear();
  schedule_.Rearm();
  task_environment_.FastForwardBy(base::Seconds(1));
  EXPECT_TRUE(IsArchived(id));
}

TEST_F(ArchiveScheduleTest, UsingATabStartsItsTimeoutAgain) {
  schedule_.SetArchiveAfter(base::Hours(12));
  const std::string id = Add(Section::kToday);
  schedule_.Rearm();
  task_environment_.FastForwardBy(base::Hours(10));
  const base::Time used = Now();
  model_.UpdateTab(id, [used](TabRecord& tab) { tab.last_active = used; });
  task_environment_.FastForwardBy(base::Hours(11));
  EXPECT_FALSE(IsArchived(id));
  task_environment_.FastForwardBy(base::Hours(1));
  EXPECT_TRUE(IsArchived(id));
}

TEST_F(ArchiveScheduleTest, TheTimerIsArmedForTheEarliestTab) {
  schedule_.SetArchiveAfter(base::Hours(12));
  Add(Section::kToday);
  task_environment_.FastForwardBy(base::Hours(2));
  const std::string later = Add(Section::kToday);
  EXPECT_EQ(schedule_.next_fire_in(), base::Hours(10));
  // Once the first goes, the timer moves on to the second.
  task_environment_.FastForwardBy(base::Hours(10));
  EXPECT_FALSE(IsArchived(later));
  EXPECT_EQ(schedule_.next_fire_in(), base::Hours(2));
}

TEST_F(ArchiveScheduleTest, OneTimerIsPendingHoweverManyTabsAndChanges) {
  schedule_.SetArchiveAfter(base::Hours(12));
  for (int i = 0; i < 50; ++i) {
    Add(Section::kToday);
  }
  schedule_.Rearm();
  schedule_.SetArchiveAfter(base::Hours(24));
  schedule_.SetKeep(base::Days(7));
  schedule_.Rearm();
  EXPECT_TRUE(schedule_.timer_pending());
  EXPECT_EQ(task_environment_.GetPendingMainThreadTaskCount(), 1u);
}

TEST_F(ArchiveScheduleTest, ARearmAsksAboutTheExemptionOfFewTabsNotOfEveryTab) {
  schedule_.SetArchiveAfter(base::Hours(12));
  std::string first;
  for (int i = 0; i < 500; ++i) {
    task_environment_.FastForwardBy(base::Seconds(1));
    const std::string id = Add(Section::kToday);
    if (first.empty()) {
      first = id;
    }
  }
  // The two oldest tabs are the selected ones of two windows.
  exempt_.insert(first);
  schedule_.Rearm();
  exempt_calls_ = 0;
  schedule_.Rearm();
  // The earliest is exempt, so it is skipped: two calls, not five hundred.
  EXPECT_LE(exempt_calls_, 3);
  EXPECT_GE(exempt_calls_, 1);
  EXPECT_TRUE(schedule_.timer_pending());
  // Without an exemption it is one call.
  exempt_.clear();
  exempt_calls_ = 0;
  schedule_.Rearm();
  EXPECT_EQ(exempt_calls_, 1);
  // And a purge that is due first needs no call at all.
  schedule_.SetArchiveAfter(std::nullopt);
  exempt_calls_ = 0;
  schedule_.Rearm();
  EXPECT_EQ(exempt_calls_, 0);
}

TEST_F(ArchiveScheduleTest, AfterShutdownNothingIsArchivedAndNothingIsArmed) {
  schedule_.SetArchiveAfter(base::Hours(12));
  const std::string id = Add(Section::kToday);
  ASSERT_TRUE(schedule_.timer_pending());
  schedule_.Shutdown();
  EXPECT_FALSE(schedule_.timer_pending());
  task_environment_.FastForwardBy(base::Days(3));
  schedule_.RunDueNow();
  schedule_.Rearm();
  Add(Section::kToday);
  EXPECT_FALSE(schedule_.timer_pending());
  EXPECT_FALSE(IsArchived(id));
  EXPECT_TRUE(archive_calls_.empty());
}

TEST_F(ArchiveScheduleTest, NothingIsPendingWithNothingToDo) {
  schedule_.SetArchiveAfter(base::Hours(12));
  schedule_.Rearm();
  EXPECT_FALSE(schedule_.timer_pending());
  EXPECT_EQ(task_environment_.GetPendingMainThreadTaskCount(), 0u);
}

TEST_F(ArchiveScheduleTest, ATabAwaitingSessionRestoreIsNotACandidate) {
  schedule_.SetArchiveAfter(base::Hours(12));
  const std::string id = Add(Section::kToday);
  ASSERT_TRUE(model_.MarkAwaitingRestore(id));
  schedule_.Rearm();
  EXPECT_FALSE(schedule_.timer_pending());
}

TEST_F(ArchiveScheduleTest, ATabThatWillNotCloseIsTriedAgainAfterAnotherTimeout) {
  schedule_.SetArchiveAfter(base::Hours(12));
  const std::string id = Add(Section::kToday);
  refuse_.insert(id);
  schedule_.Rearm();
  task_environment_.FastForwardBy(base::Hours(12));
  EXPECT_EQ(archive_calls_.size(), 1u);
  EXPECT_FALSE(IsArchived(id));
  task_environment_.FastForwardBy(base::Hours(11));
  EXPECT_EQ(archive_calls_.size(), 1u);
  refuse_.clear();
  task_environment_.FastForwardBy(base::Hours(1));
  EXPECT_TRUE(IsArchived(id));
}

TEST_F(ArchiveScheduleTest, OldArchivedRecordsAreDroppedWhenTheirTimeComes) {
  schedule_.SetArchiveAfter(base::Hours(12));
  schedule_.SetKeep(base::Days(30));
  const std::string id = Add(Section::kToday);
  schedule_.Rearm();
  task_environment_.FastForwardBy(base::Hours(12));
  ASSERT_TRUE(IsArchived(id));
  task_environment_.FastForwardBy(base::Days(30) - base::Hours(1));
  EXPECT_TRUE(model_.GetTab(id));
  task_environment_.FastForwardBy(base::Hours(1));
  EXPECT_FALSE(model_.GetTab(id));
  EXPECT_FALSE(schedule_.timer_pending());
}

TEST_F(ArchiveScheduleTest, ForeverKeepsRecordsAndLeavesNoTimer) {
  schedule_.SetArchiveAfter(base::Hours(12));
  schedule_.SetKeep(std::nullopt);
  const std::string id = Add(Section::kToday);
  schedule_.Rearm();
  task_environment_.FastForwardBy(base::Hours(12));
  ASSERT_TRUE(IsArchived(id));
  EXPECT_FALSE(schedule_.timer_pending());
  task_environment_.FastForwardBy(base::Days(400));
  EXPECT_TRUE(model_.GetTab(id));
}

TEST_F(ArchiveScheduleTest, ARearmReadsTheWallClockAgain) {
  // After the computer slept, the owner calls Rearm: the monotonic timer did not
  // count the time asleep, the records' wall-clock times did.
  schedule_.SetArchiveAfter(base::Hours(12));
  const std::string id = Add(Section::kToday);
  schedule_.Rearm();
  EXPECT_EQ(schedule_.next_fire_in(), base::Hours(12));
  const base::Time idle_since = Now() - base::Hours(1);
  model_.UpdateTab(id, [idle_since](TabRecord& tab) {
    tab.last_active = idle_since;
  });
  schedule_.Rearm();
  EXPECT_EQ(schedule_.next_fire_in(), base::Hours(11));
}

}  // namespace

}  // namespace aerie_tab_model
