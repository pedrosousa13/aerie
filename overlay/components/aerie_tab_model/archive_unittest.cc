// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_tab_model/archive.h"

#include <string>
#include <vector>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/test/simple_test_clock.h"
#include "components/aerie_tab_model/folder_tree.h"
#include "components/aerie_tab_model/tab_model.h"
#include "components/aerie_tab_model/tab_model_types.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace aerie_tab_model {

namespace {

class ArchiveTest : public testing::Test {
 protected:
  ArchiveTest() : model_(&clock_) {
    // Noon, so that hours either way stay on the same local day.
    base::Time noon;
    EXPECT_TRUE(base::Time::FromLocalExploded(
        {2026, 11, 5, 13, 12, 0, 0, 0}, &noon));
    clock_.SetNow(noon);
  }

  std::string AddToday(const std::string& parent = std::string(),
                       int position = 0) {
    TabRecord tab;
    tab.section = Section::kToday;
    tab.parent = parent;
    tab.position = position;
    tab.last_active = clock_.Now();
    return model_.AddTab(std::move(tab));
  }

  static ArchiveData Data(const std::string& url = "https://example.com/a",
                          const std::string& title = "Example A") {
    ArchiveData data;
    data.url = url;
    data.title = title;
    data.favicon_key = url;
    data.entries = {{"https://example.com/", "Example"}, {url, title}};
    return data;
  }

  base::SimpleTestClock clock_;
  TabModel model_;
};

TEST_F(ArchiveTest, ArchivingATodayTabStampsItAndKeepsItsData) {
  const std::string folder =
      CreateFolder(model_, Section::kToday, std::string(), "Reading");
  const std::string id = AddToday(folder);
  ASSERT_TRUE(ArchiveTab(model_, id, Data(), clock_.Now()));
  const TabRecord* tab = model_.GetTab(id);
  ASSERT_TRUE(tab);
  EXPECT_EQ(tab->archived_at, clock_.Now());
  ASSERT_TRUE(tab->archive);
  EXPECT_EQ(tab->archive->url, "https://example.com/a");
  EXPECT_EQ(tab->archive->title, "Example A");
  EXPECT_EQ(tab->archive->folder, folder);
  EXPECT_EQ(tab->archive->entries.size(), 2u);
}

TEST_F(ArchiveTest, OnlyAnUnarchivedTodayTabArchives) {
  TabRecord pinned;
  pinned.section = Section::kPinned;
  const std::string pinned_id = model_.AddTab(std::move(pinned));
  EXPECT_FALSE(ArchiveTab(model_, pinned_id, Data(), clock_.Now()));
  EXPECT_FALSE(ArchiveTab(model_, "nope", Data(), clock_.Now()));
  const std::string id = AddToday();
  EXPECT_TRUE(ArchiveTab(model_, id, Data(), clock_.Now()));
  EXPECT_FALSE(ArchiveTab(model_, id, Data(), clock_.Now()));
}

TEST_F(ArchiveTest, TheArchiveKeepsOnlyTheLastFiveEntries) {
  const std::string id = AddToday();
  ArchiveData data = Data();
  data.entries.clear();
  for (int i = 0; i < 8; ++i) {
    data.entries.push_back({"https://example.com/" + std::to_string(i), "t"});
  }
  ASSERT_TRUE(ArchiveTab(model_, id, std::move(data), clock_.Now()));
  const auto& entries = model_.GetTab(id)->archive->entries;
  ASSERT_EQ(entries.size(), kMaxArchivedNavigationEntries);
  EXPECT_EQ(entries.front().url, "https://example.com/3");
  EXPECT_EQ(entries.back().url, "https://example.com/7");
}

TEST_F(ArchiveTest, AnArchivedRecordSurvivesTheJsonRoundTrip) {
  const std::string id = AddToday();
  ASSERT_TRUE(ArchiveTab(model_, id, Data(), clock_.Now()));
  const base::DictValue json = model_.ToValue();
  ParseResult parsed = ParseState(json);
  ASSERT_EQ(parsed.status, LoadStatus::kOk);
  const TabRecord* found = nullptr;
  for (const TabRecord& tab : parsed.state->tabs) {
    if (tab.id == id) {
      found = &tab;
    }
  }
  ASSERT_TRUE(found);
  EXPECT_EQ(found->archive, model_.GetTab(id)->archive);
  EXPECT_EQ(found->archived_at, model_.GetTab(id)->archived_at);
}

TEST_F(ArchiveTest, ARecordWithABadArchiveBlockMakesTheFileCorrupt) {
  const std::string id = AddToday();
  ASSERT_TRUE(ArchiveTab(model_, id, Data(), clock_.Now()));
  base::DictValue json = model_.ToValue();
  for (base::Value& tab : *json.FindList("tabs")) {
    tab.GetDict().FindDict("archive")->Set("url", 5);
  }
  EXPECT_EQ(ParseState(json).status, LoadStatus::kCorrupt);
}

TEST_F(ArchiveTest, OnlyAnAddressChromiumMayLoadIsRestorable) {
  EXPECT_TRUE(IsRestorableArchiveUrl("https://example.com/a?b=c#d"));
  EXPECT_TRUE(IsRestorableArchiveUrl("http://127.0.0.1:8000/x"));
  EXPECT_TRUE(IsRestorableArchiveUrl("chrome://settings/"));
  EXPECT_FALSE(IsRestorableArchiveUrl(""));
  EXPECT_FALSE(IsRestorableArchiveUrl("not a url"));
  EXPECT_FALSE(IsRestorableArchiveUrl("javascript:alert(1)"));
  EXPECT_FALSE(IsRestorableArchiveUrl("JaVaScRiPt:alert(document.cookie)"));
  EXPECT_FALSE(IsRestorableArchiveUrl("  javascript:alert(1)"));
  EXPECT_FALSE(IsRestorableArchiveUrl(
      "https://example.com/" + std::string(3 * 1024 * 1024, 'a')));
}

TEST_F(ArchiveTest, HostileEntriesInTheFileAreDroppedWhenItIsRead) {
  const std::string id = AddToday();
  ASSERT_TRUE(ArchiveTab(model_, id, Data(), clock_.Now()));
  base::DictValue json = model_.ToValue();
  for (base::Value& tab : *json.FindList("tabs")) {
    base::DictValue* archive = tab.GetDict().FindDict("archive");
    ASSERT_TRUE(archive);
    base::ListValue entries;
    for (const char* url :
         {"javascript:alert(document.domain)", "https://example.com/ok",
          "garbage", ""}) {
      base::DictValue item;
      item.Set("url", url);
      item.Set("title", "t");
      entries.Append(std::move(item));
    }
    archive->Set("entries", std::move(entries));
    archive->Set("url", "javascript:alert(1)");
  }
  ParseResult parsed = ParseState(json);
  ASSERT_EQ(parsed.status, LoadStatus::kOk);
  const TabRecord* found = nullptr;
  for (const TabRecord& tab : parsed.state->tabs) {
    if (tab.id == id) {
      found = &tab;
    }
  }
  ASSERT_TRUE(found && found->archive);
  ASSERT_EQ(found->archive->entries.size(), 1u);
  EXPECT_EQ(found->archive->entries[0].url, "https://example.com/ok");
  // The shown address is never the refused one.
  EXPECT_EQ(found->archive->url, "https://example.com/ok");
}

TEST_F(ArchiveTest, ARecordWithNoLoadableEntryShowsNoRowAndIsStillDropped) {
  const std::string id = AddToday();
  ASSERT_TRUE(ArchiveTab(model_, id, Data(), clock_.Now()));
  base::DictValue json = model_.ToValue();
  for (base::Value& tab : *json.FindList("tabs")) {
    base::DictValue* archive = tab.GetDict().FindDict("archive");
    base::ListValue entries;
    base::DictValue item;
    item.Set("url", "javascript:void(0)");
    item.Set("title", "t");
    entries.Append(std::move(item));
    archive->Set("entries", std::move(entries));
  }
  TabModel reloaded(&clock_);
  reloaded.MergeLoaded(std::move(ParseState(json).state));
  ASSERT_TRUE(reloaded.GetTab(id));
  EXPECT_TRUE(ArchivedRows(reloaded).empty());
  clock_.Advance(base::Days(31));
  EXPECT_EQ(PurgeArchive(reloaded, clock_.Now(), base::Days(30)), 1u);
}

TEST_F(ArchiveTest, AnArchivedRecordIsStoredNotWaitingForARestoredTab) {
  const std::string id = AddToday();
  ASSERT_TRUE(ArchiveTab(model_, id, Data(), clock_.Now()));
  TabModel reloaded(&clock_);
  ParseResult parsed = ParseState(model_.ToValue());
  reloaded.MergeLoaded(std::move(parsed.state));
  ASSERT_TRUE(reloaded.GetTab(id));
  EXPECT_FALSE(reloaded.IsAwaitingRestore(id));
  reloaded.RemoveUnclaimedRestoredTabs();
  EXPECT_TRUE(reloaded.GetTab(id));
}

TEST_F(ArchiveTest, RestoringPutsTheTabAtTheTopOfTodayAndClearsTheArchive) {
  const std::string other = AddToday(std::string(), 3);
  const std::string id = AddToday(std::string(), 7);
  ASSERT_TRUE(ArchiveTab(model_, id, Data(), clock_.Now()));
  clock_.Advance(base::Hours(2));
  ASSERT_TRUE(RestoreArchivedTab(model_, id, clock_.Now()));
  const TabRecord* tab = model_.GetTab(id);
  EXPECT_FALSE(tab->archived_at);
  EXPECT_FALSE(tab->archive);
  EXPECT_EQ(tab->last_active, clock_.Now());
  EXPECT_EQ(tab->section, Section::kToday);
  EXPECT_EQ(tab->parent, "");
  EXPECT_LT(tab->position, model_.GetTab(other)->position);
}

TEST_F(ArchiveTest, RestoringGoesBackIntoTheSameFolderAndOpensIt) {
  const std::string folder =
      CreateFolder(model_, Section::kToday, std::string(), "Reading");
  SetFolderExpanded(model_, folder, false);
  const std::string sibling = AddToday(folder, 4);
  const std::string id = AddToday(folder, 9);
  ASSERT_TRUE(ArchiveTab(model_, id, Data(), clock_.Now()));
  ASSERT_TRUE(RestoreArchivedTab(model_, id, clock_.Now()));
  const TabRecord* tab = model_.GetTab(id);
  EXPECT_EQ(tab->parent, folder);
  EXPECT_LT(tab->position, model_.GetTab(sibling)->position);
  EXPECT_TRUE(model_.GetFolder(folder)->expanded);
}

TEST_F(ArchiveTest, RestoringAfterTheFolderWasDeletedGoesToTopOfToday) {
  const std::string folder =
      CreateFolder(model_, Section::kToday, std::string(), "Reading");
  const std::string id = AddToday(folder, 0);
  const std::string other = AddToday(std::string(), 2);
  ASSERT_TRUE(ArchiveTab(model_, id, Data(), clock_.Now()));
  ASSERT_TRUE(DeleteFolder(model_, folder));
  ASSERT_TRUE(RestoreArchivedTab(model_, id, clock_.Now()));
  const TabRecord* tab = model_.GetTab(id);
  EXPECT_EQ(tab->parent, "");
  EXPECT_LT(tab->position, model_.GetTab(other)->position);
}

TEST_F(ArchiveTest, RestoringAnUnarchivedOrUnknownRecordDoesNothing) {
  const std::string id = AddToday();
  EXPECT_FALSE(RestoreArchivedTab(model_, id, clock_.Now()));
  EXPECT_FALSE(RestoreArchivedTab(model_, "nope", clock_.Now()));
}

TEST_F(ArchiveTest, RecordsOlderThanThirtyDaysAreDropped) {
  const std::string old_id = AddToday();
  const std::string fresh_id = AddToday();
  ASSERT_TRUE(ArchiveTab(model_, old_id, Data(), clock_.Now()));
  clock_.Advance(base::Days(20));
  ASSERT_TRUE(ArchiveTab(model_, fresh_id, Data(), clock_.Now()));
  clock_.Advance(base::Days(10) + base::Minutes(1));
  EXPECT_EQ(PurgeArchive(model_, clock_.Now(), base::Days(30)), 1u);
  EXPECT_FALSE(model_.GetTab(old_id));
  EXPECT_TRUE(model_.GetTab(fresh_id));
}

TEST_F(ArchiveTest, ARecordJustInsideTheRetentionStays) {
  const std::string id = AddToday();
  ASSERT_TRUE(ArchiveTab(model_, id, Data(), clock_.Now()));
  clock_.Advance(base::Days(30) - base::Seconds(1));
  EXPECT_EQ(PurgeArchive(model_, clock_.Now(), base::Days(30)), 0u);
  clock_.Advance(base::Seconds(1));
  EXPECT_EQ(PurgeArchive(model_, clock_.Now(), base::Days(30)), 1u);
}

TEST_F(ArchiveTest, ForeverKeepsEverythingAndPurgeNeverTouchesOpenTabs) {
  const std::string archived = AddToday();
  const std::string open_tab = AddToday();
  ASSERT_TRUE(ArchiveTab(model_, archived, Data(), clock_.Now()));
  clock_.Advance(base::Days(900));
  EXPECT_EQ(PurgeArchive(model_, clock_.Now(), std::nullopt), 0u);
  EXPECT_EQ(PurgeArchive(model_, clock_.Now(), base::Days(90)), 1u);
  EXPECT_TRUE(model_.GetTab(open_tab));
}

TEST_F(ArchiveTest, TheNextPurgeIsTheOldestRecordPlusTheRetention) {
  EXPECT_EQ(NextPurgeTime(model_, base::Days(30)), std::nullopt);
  const std::string a = AddToday();
  ASSERT_TRUE(ArchiveTab(model_, a, Data(), clock_.Now()));
  const base::Time first = clock_.Now();
  clock_.Advance(base::Days(1));
  const std::string b = AddToday();
  ASSERT_TRUE(ArchiveTab(model_, b, Data(), clock_.Now()));
  EXPECT_EQ(NextPurgeTime(model_, base::Days(30)), first + base::Days(30));
  EXPECT_EQ(NextPurgeTime(model_, std::nullopt), std::nullopt);
}

TEST_F(ArchiveTest, RowsAreNewestFirstAndSearchMatchesTitleAndAddress) {
  const std::string a = AddToday();
  ASSERT_TRUE(ArchiveTab(model_, a, Data("https://alpha.test/x", "Alpha Page"),
                         clock_.Now()));
  clock_.Advance(base::Minutes(5));
  const std::string b = AddToday();
  ASSERT_TRUE(ArchiveTab(model_, b, Data("https://beta.test/Docs", "Beta"),
                         clock_.Now()));
  AddToday();  // Open: not a row.
  const std::vector<ArchivedRow> rows = ArchivedRows(model_);
  ASSERT_EQ(rows.size(), 2u);
  EXPECT_EQ(rows[0].id, b);
  EXPECT_EQ(rows[1].id, a);
  EXPECT_EQ(rows[0].workspace_id, model_.workspace().id);

  EXPECT_EQ(SearchArchive(rows, "").size(), 2u);
  EXPECT_EQ(SearchArchive(rows, "ALPHA").at(0).id, a);   // title and address
  EXPECT_EQ(SearchArchive(rows, "alpha page").at(0).id, a);
  EXPECT_EQ(SearchArchive(rows, "/docs").at(0).id, b);   // address only
  EXPECT_TRUE(SearchArchive(rows, "gamma").empty());
}

TEST_F(ArchiveTest, GroupsFollowTheLocalCalendarDay) {
  const base::Time now = clock_.Now();  // 12:00 on the 13th.
  EXPECT_EQ(GroupFor(now - base::Hours(11), now), ArchiveGroup::kToday);
  EXPECT_EQ(GroupFor(now - base::Hours(13), now), ArchiveGroup::kYesterday);
  EXPECT_EQ(GroupFor(now - base::Hours(36), now), ArchiveGroup::kYesterday);
  EXPECT_EQ(GroupFor(now - base::Hours(37), now), ArchiveGroup::kLastWeek);
  EXPECT_EQ(GroupFor(now - base::Days(7), now), ArchiveGroup::kLastWeek);
  EXPECT_EQ(GroupFor(now - base::Days(8), now), ArchiveGroup::kEarlier);
}

TEST_F(ArchiveTest, GroupArchiveSkipsEmptyGroupsAndKeepsOrder) {
  std::vector<ArchivedRow> rows;
  for (base::TimeDelta age : {base::Hours(1), base::Days(3), base::Days(40)}) {
    ArchivedRow row;
    row.id = std::to_string(age.InHours());
    row.archived_at = clock_.Now() - age;
    rows.push_back(row);
  }
  const auto sections = GroupArchive(rows, clock_.Now());
  ASSERT_EQ(sections.size(), 3u);
  EXPECT_EQ(sections[0].group, ArchiveGroup::kToday);
  EXPECT_EQ(sections[1].group, ArchiveGroup::kLastWeek);
  EXPECT_EQ(sections[2].group, ArchiveGroup::kEarlier);
}

}  // namespace

}  // namespace aerie_tab_model
