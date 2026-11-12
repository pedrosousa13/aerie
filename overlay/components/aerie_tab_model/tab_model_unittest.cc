// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_tab_model/tab_model.h"

#include <memory>
#include <span>
#include <string>
#include <vector>

#include "base/functional/bind.h"
#include "base/json/values_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/time/default_clock.h"
#include "base/test/simple_test_clock.h"
#include "base/time/time.h"
#include "base/values.h"
#include "components/aerie_tab_model/tab_model_types.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace aerie_tab_model {

namespace {

// Remembers what the observer interface was told.
class RecordingObserver : public TabModelObserver {
 public:
  void OnTabAdded(const TabRecord& tab) override { events.push_back("tab-added " + tab.id); }
  void OnTabChanged(const TabRecord& tab) override { events.push_back("tab-changed " + tab.id); }
  void OnTabMoved(const TabRecord& tab, const Placement& from) override {
    events.push_back("tab-moved " + tab.id + " from " + SectionToString(from.section) +
                     "/" + from.parent + "/" + base::NumberToString(from.position));
  }
  void OnTabRemoved(const TabRecord& tab) override { events.push_back("tab-removed " + tab.id); }
  void OnFolderAdded(const FolderRecord& folder) override { events.push_back("folder-added " + folder.id); }
  void OnFolderChanged(const FolderRecord& folder) override { events.push_back("folder-changed " + folder.id); }
  void OnFolderMoved(const FolderRecord& folder, const Placement&) override {
    events.push_back("folder-moved " + folder.id);
  }
  void OnFolderRemoved(const FolderRecord& folder) override { events.push_back("folder-removed " + folder.id); }
  void OnWorkspaceChanged(const Workspace&) override { events.push_back("workspace-changed"); }
  void OnModelLoaded() override { events.push_back("loaded"); }
  void OnModelChanged() override { ++changes; }

  std::vector<std::string> events;
  int changes = 0;
};

class TabModelTest : public testing::Test {
 protected:
  TabModelTest() {
    clock_.SetNow(base::Time::FromMillisecondsSinceUnixEpoch(1'700'000'000'000));
    model_ = MakeModel();
    model_->AddObserver(&observer_);
  }
  ~TabModelTest() override { model_->RemoveObserver(&observer_); }

  std::unique_ptr<TabModel> MakeModel(size_t max_log = kMaxChangeLogEntries) {
    return std::make_unique<TabModel>(
        &clock_,
        base::BindRepeating(
            [](int* next) { return "id-" + base::NumberToString((*next)++); },
            &next_id_),
        max_log);
  }

  std::string AddTab(Section section = Section::kToday, const std::string& parent = std::string()) {
    TabRecord tab;
    tab.section = section;
    tab.parent = parent;
    tab.last_active = clock_.Now();
    return model_->AddTab(std::move(tab));
  }

  std::string AddFolder(const std::string& parent = std::string()) {
    FolderRecord folder;
    folder.name = "Folder";
    folder.parent = parent;
    return model_->AddFolder(std::move(folder));
  }

  base::SimpleTestClock clock_;
  int next_id_ = 1;
  std::unique_ptr<TabModel> model_;
  RecordingObserver observer_;
};

TEST_F(TabModelTest, FreshModelHasOnePersonalWorkspace) {
  const Workspace& workspace = model_->workspace();
  EXPECT_EQ(workspace.id, kDefaultWorkspaceId);
  EXPECT_EQ(workspace.name, "Personal");
  EXPECT_EQ(workspace.colour, "dusk");
  EXPECT_EQ(workspace.theme, "default");
  EXPECT_EQ(workspace.mode, "default");
  EXPECT_EQ(workspace.created, clock_.Now());
}

TEST_F(TabModelTest, CreatingTheWorkspaceIsLoggedWhenNothingIsStored) {
  EXPECT_TRUE(model_->change_log().empty());
  model_->MergeLoaded(std::nullopt);
  ASSERT_EQ(model_->change_log().size(), 1u);
  const ChangeRecord& change = model_->change_log().front();
  EXPECT_EQ(change.type, ChangeType::kAdded);
  EXPECT_EQ(change.kind, RecordKind::kWorkspace);
  EXPECT_EQ(change.record_id, kDefaultWorkspaceId);
  EXPECT_EQ(model_->workspace().change, change.counter);
  EXPECT_EQ(observer_.events, std::vector<std::string>{"loaded"});
}

TEST_F(TabModelTest, AddTabSetsIdTimesWorkspaceAndLogsIt) {
  clock_.Advance(base::Seconds(5));
  std::string id = AddTab();
  const TabRecord* tab = model_->GetTab(id);
  ASSERT_TRUE(tab);
  EXPECT_FALSE(id.empty());
  EXPECT_EQ(tab->workspace_id, kDefaultWorkspaceId);
  EXPECT_EQ(tab->created, clock_.Now());
  EXPECT_EQ(tab->modified, clock_.Now());
  ASSERT_EQ(model_->change_log().size(), 1u);
  const ChangeRecord& change = model_->change_log().back();
  EXPECT_EQ(change.type, ChangeType::kAdded);
  EXPECT_EQ(change.kind, RecordKind::kTab);
  EXPECT_EQ(change.record_id, id);
  EXPECT_EQ(change.time, clock_.Now());
  EXPECT_EQ(tab->change, change.counter);
  EXPECT_EQ(observer_.events, std::vector<std::string>{"tab-added " + id});
  EXPECT_EQ(observer_.changes, 1);
}

TEST_F(TabModelTest, UpdateFollowsTheClockAndKeepsCreated) {
  std::string id = AddTab();
  base::Time created = model_->GetTab(id)->created;
  clock_.Advance(base::Minutes(3));
  EXPECT_TRUE(model_->UpdateTab(id, [&](TabRecord& tab) {
    tab.manual_name = "Inbox";
    tab.sleeping = true;
  }));
  const TabRecord* tab = model_->GetTab(id);
  EXPECT_EQ(tab->manual_name, "Inbox");
  EXPECT_TRUE(tab->sleeping);
  EXPECT_EQ(tab->created, created);
  EXPECT_EQ(tab->modified, clock_.Now());
  EXPECT_EQ(model_->change_log().back().type, ChangeType::kChanged);
  EXPECT_EQ(model_->change_log().back().time, clock_.Now());
}

TEST_F(TabModelTest, UpdateCannotChangeIdentityOrPlacement) {
  std::string id = AddTab(Section::kPinned);
  EXPECT_TRUE(model_->UpdateTab(id, [](TabRecord& tab) {
    tab.id = "other";
    tab.section = Section::kToday;
    tab.position = 9;
    tab.workspace_id = "elsewhere";
    tab.parent = "nowhere";
  }));
  const TabRecord* tab = model_->GetTab(id);
  ASSERT_TRUE(tab);
  EXPECT_EQ(tab->id, id);
  EXPECT_EQ(tab->section, Section::kPinned);
  EXPECT_EQ(tab->position, 0);
  EXPECT_EQ(tab->workspace_id, kDefaultWorkspaceId);
  EXPECT_EQ(tab->parent, "");
  EXPECT_EQ(model_->GetTab("other"), nullptr);
}

TEST_F(TabModelTest, UpdateOfAnUnknownIdFails) {
  EXPECT_FALSE(model_->UpdateTab("nope", [](TabRecord&) {}));
  EXPECT_FALSE(model_->UpdateFolder("nope", [](FolderRecord&) {}));
  EXPECT_TRUE(model_->change_log().empty());
}

TEST_F(TabModelTest, MoveLogsAndTellsTheObserverWhereTheTabWas) {
  std::string folder = AddFolder();
  std::string id = AddTab();
  clock_.Advance(base::Seconds(1));
  EXPECT_TRUE(model_->MoveTab(id, Section::kPinned, folder, 4));
  const TabRecord* tab = model_->GetTab(id);
  EXPECT_EQ(tab->section, Section::kPinned);
  EXPECT_EQ(tab->parent, folder);
  EXPECT_EQ(tab->position, 4);
  EXPECT_EQ(tab->modified, clock_.Now());
  EXPECT_EQ(model_->change_log().back().type, ChangeType::kMoved);
  EXPECT_EQ(observer_.events.back(), "tab-moved " + id + " from today//0");
}

TEST_F(TabModelTest, MoveIntoAnUnknownFolderFailsAndChangesNothing) {
  std::string id = AddTab();
  size_t log = model_->change_log().size();
  EXPECT_FALSE(model_->MoveTab(id, Section::kPinned, "nope", 0));
  EXPECT_EQ(model_->GetTab(id)->section, Section::kToday);
  EXPECT_EQ(model_->change_log().size(), log);
}

TEST_F(TabModelTest, AddUnderAnUnknownFolderFails) {
  EXPECT_EQ(AddTab(Section::kToday, "nope"), "");
  EXPECT_TRUE(model_->tabs().empty());
  EXPECT_TRUE(model_->change_log().empty());
}

TEST_F(TabModelTest, RemoveLogsAndTellsTheObserver) {
  std::string id = AddTab();
  EXPECT_TRUE(model_->RemoveTab(id));
  EXPECT_EQ(model_->GetTab(id), nullptr);
  EXPECT_EQ(model_->change_log().back().type, ChangeType::kRemoved);
  EXPECT_EQ(model_->change_log().back().record_id, id);
  EXPECT_EQ(observer_.events.back(), "tab-removed " + id);
  EXPECT_FALSE(model_->RemoveTab(id));
}

TEST_F(TabModelTest, RemovingAFolderMovesWhatWasInsideUpOneLevel) {
  std::string outer = AddFolder();
  std::string inner = AddFolder(outer);
  std::string tab = AddTab(Section::kToday, inner);
  std::string sub = AddFolder(inner);
  EXPECT_TRUE(model_->RemoveFolder(inner));
  EXPECT_EQ(model_->GetFolder(inner), nullptr);
  EXPECT_EQ(model_->GetTab(tab)->parent, outer);
  EXPECT_EQ(model_->GetFolder(sub)->parent, outer);
}

TEST_F(TabModelTest, AFolderCannotMoveIntoItselfOrBelowItself) {
  std::string outer = AddFolder();
  std::string inner = AddFolder(outer);
  EXPECT_FALSE(model_->MoveFolder(outer, Section::kToday, outer, 0));
  EXPECT_FALSE(model_->MoveFolder(outer, Section::kToday, inner, 0));
  EXPECT_TRUE(model_->MoveFolder(inner, Section::kToday, "", 0));
  EXPECT_EQ(model_->GetFolder(inner)->parent, "");
}

TEST_F(TabModelTest, FavoritesStopAtTheCap) {
  for (size_t i = 0; i < kFavoritesCap; ++i) {
    EXPECT_FALSE(AddTab(Section::kFavorite).empty());
  }
  EXPECT_EQ(AddTab(Section::kFavorite), "");
  std::string today = AddTab();
  EXPECT_FALSE(model_->MoveTab(today, Section::kFavorite, "", 0));
  EXPECT_EQ(model_->GetTab(today)->section, Section::kToday);
}

TEST_F(TabModelTest, ReorderingInsideFavoritesIsNotBlockedByTheCap) {
  std::string first;
  for (size_t i = 0; i < kFavoritesCap; ++i) {
    std::string id = AddTab(Section::kFavorite);
    if (first.empty()) {
      first = id;
    }
  }
  EXPECT_TRUE(model_->MoveTab(first, Section::kFavorite, "", 7));
}

TEST_F(TabModelTest, TheChangeLogIsBounded) {
  model_ = MakeModel(/*max_log=*/5);
  for (int i = 0; i < 9; ++i) {
    AddTab();
  }
  ASSERT_EQ(model_->change_log().size(), 5u);
  // The newest five stay, in order.
  EXPECT_EQ(model_->change_log().front().counter, 5);
  EXPECT_EQ(model_->change_log().back().counter, 9);
  EXPECT_EQ(model_->next_change(), 10);
}

TEST_F(TabModelTest, CountersOnlyGrow) {
  std::string a = AddTab();
  std::string b = AddTab();
  model_->MoveTab(a, Section::kPinned, "", 0);
  model_->UpdateTab(b, [](TabRecord& tab) { tab.sleeping = true; });
  int64_t previous = 0;
  for (const ChangeRecord& change : model_->change_log()) {
    EXPECT_GT(change.counter, previous);
    previous = change.counter;
  }
  EXPECT_EQ(model_->GetTab(a)->change, 3);
  EXPECT_EQ(model_->GetTab(b)->change, 4);
}

TEST_F(TabModelTest, UpdatingTheWorkspaceStoresColourThemeAndMode) {
  clock_.Advance(base::Seconds(2));
  model_->UpdateWorkspace([](Workspace& workspace) {
    workspace.colour = "#336699";
    workspace.theme = "oled";
    workspace.mode = "dark";
    workspace.id = "changed";
  });
  EXPECT_EQ(model_->workspace().id, kDefaultWorkspaceId);
  EXPECT_EQ(model_->workspace().colour, "#336699");
  EXPECT_EQ(model_->workspace().theme, "oled");
  EXPECT_EQ(model_->workspace().mode, "dark");
  EXPECT_EQ(model_->workspace().modified, clock_.Now());
  EXPECT_EQ(observer_.events.back(), "workspace-changed");
}

TEST_F(TabModelTest, ValueRoundTripKeepsEveryField) {
  model_->MergeLoaded(std::nullopt);
  std::string folder = AddFolder();
  model_->UpdateFolder(folder, [](FolderRecord& f) {
    f.kind = FolderKind::kSmart;
    f.icon = "star";
    base::DictValue rules;
    rules.Set("host", "example.com");
    f.rules = std::move(rules);
    base::DictValue behaviour;
    behaviour.Set("collapse", true);
    f.behaviour = std::move(behaviour);
  });
  clock_.Advance(base::Seconds(9));
  TabRecord tab;
  tab.section = Section::kPinned;
  tab.parent = folder;
  tab.position = 3;
  tab.pinned_url = "https://example.com/";
  tab.manual_name = "Example";
  tab.last_active = clock_.Now();
  tab.sleeping = true;
  tab.archived_at = clock_.Now();
  std::string id = model_->AddTab(std::move(tab));

  ParseResult parsed = ParseState(model_->ToValue());
  ASSERT_EQ(parsed.status, LoadStatus::kOk);
  const ModelState& state = *parsed.state;
  EXPECT_EQ(state.next_change, model_->next_change());
  EXPECT_EQ(state.workspace.colour, "dusk");
  ASSERT_EQ(state.tabs.size(), 1u);
  const TabRecord& read = state.tabs[0];
  const TabRecord& written = *model_->GetTab(id);
  EXPECT_EQ(read.id, id);
  EXPECT_EQ(read.workspace_id, written.workspace_id);
  EXPECT_EQ(read.section, Section::kPinned);
  EXPECT_EQ(read.parent, folder);
  EXPECT_EQ(read.position, 3);
  EXPECT_EQ(read.pinned_url, "https://example.com/");
  EXPECT_EQ(read.manual_name, "Example");
  EXPECT_EQ(read.last_active, written.last_active);
  EXPECT_TRUE(read.sleeping);
  EXPECT_EQ(read.archived_at, written.archived_at);
  EXPECT_EQ(read.created, written.created);
  EXPECT_EQ(read.modified, written.modified);
  EXPECT_EQ(read.change, written.change);
  ASSERT_EQ(state.folders.size(), 1u);
  EXPECT_EQ(state.folders[0].kind, FolderKind::kSmart);
  EXPECT_EQ(state.folders[0].icon, "star");
  ASSERT_TRUE(state.folders[0].rules);
  EXPECT_EQ(*state.folders[0].rules->FindString("host"), "example.com");
  ASSERT_TRUE(state.folders[0].behaviour);
  EXPECT_EQ(state.changes.size(), model_->change_log().size());
}

TEST_F(TabModelTest, MergeKeepsStoredRecordsAndPutsThisRunsCountersBehindThem) {
  // A stored model: one pinned tab, one sleeping Today tab, one Today tab that
  // was open when it was saved.
  TabModel earlier(&clock_, base::BindRepeating([](int* n) { return "old-" + base::NumberToString((*n)++); }, &next_id_));
  earlier.MergeLoaded(std::nullopt);
  TabRecord pinned;
  pinned.section = Section::kPinned;
  std::string pinned_id = earlier.AddTab(std::move(pinned));
  TabRecord sleeping;
  sleeping.sleeping = true;
  std::string sleeping_id = earlier.AddTab(std::move(sleeping));
  std::string open_id = earlier.AddTab(TabRecord());
  ParseResult stored = ParseState(earlier.ToValue());
  ASSERT_EQ(stored.status, LoadStatus::kOk);
  int64_t stored_next = stored.state->next_change;

  // This run opened a tab before the read finished.
  std::string live = AddTab();
  EXPECT_EQ(model_->GetTab(live)->change, 1);

  model_->MergeLoaded(std::move(stored.state));

  EXPECT_TRUE(model_->GetTab(pinned_id));
  EXPECT_TRUE(model_->GetTab(sleeping_id));
  // The open Today tab waits for a restored tab to claim it (issue #223): it is
  // still there until the restore pass ends.
  ASSERT_TRUE(model_->GetTab(open_id));
  EXPECT_TRUE(model_->IsAwaitingRestore(open_id));
  EXPECT_FALSE(model_->IsAwaitingRestore(pinned_id));
  EXPECT_FALSE(model_->IsAwaitingRestore(sleeping_id));
  EXPECT_EQ(model_->RemoveUnclaimedRestoredTabs(), 1u);
  EXPECT_EQ(model_->GetTab(open_id), nullptr);
  ASSERT_TRUE(model_->GetTab(live));
  EXPECT_EQ(model_->GetTab(live)->change, stored_next);
  // The stored log comes first, then this run's, then the removal of the stale tab.
  int64_t previous = 0;
  for (const ChangeRecord& change : model_->change_log()) {
    EXPECT_GT(change.counter, previous);
    previous = change.counter;
  }
  EXPECT_EQ(model_->change_log().back().type, ChangeType::kRemoved);
  EXPECT_EQ(model_->change_log().back().record_id, open_id);
}

TEST_F(TabModelTest, MergeUsesTheStoredWorkspace) {
  TabModel earlier(&clock_, base::BindRepeating([](int* n) { return "old-" + base::NumberToString((*n)++); }, &next_id_));
  earlier.MergeLoaded(std::nullopt);
  earlier.UpdateWorkspace([](Workspace& w) {
    w.colour = "tide";
    w.theme = "soft";
    w.mode = "system";
  });
  ParseResult stored = ParseState(earlier.ToValue());
  ASSERT_EQ(stored.status, LoadStatus::kOk);
  model_->MergeLoaded(std::move(stored.state));
  EXPECT_EQ(model_->workspace().colour, "tide");
  EXPECT_EQ(model_->workspace().theme, "soft");
  EXPECT_EQ(model_->workspace().mode, "system");
}

// The join with session restore (issue #223).

class TabModelRestoreJoinTest : public TabModelTest {
 protected:
  // A model that was saved with some Today tabs and a Today folder, and is read
  // by model_. `open_`, `in_folder_` and `asleep_` are the stored ids.
  void StoreAndLoad() {
    TabModel earlier(&clock_, base::BindRepeating([](int* n) { return "old-" + base::NumberToString((*n)++); }, &next_id_));
    earlier.MergeLoaded(std::nullopt);
    folder_ = [&] {
      FolderRecord folder;
      folder.name = "Work";
      folder.section = Section::kToday;
      return earlier.AddFolder(std::move(folder));
    }();
    TabRecord named;
    named.manual_name = "My name";
    named.position = 3;
    open_ = earlier.AddTab(std::move(named));
    TabRecord filed;
    filed.parent = folder_;
    filed.position = 1;
    in_folder_ = earlier.AddTab(std::move(filed));
    TabRecord sleeping;
    sleeping.sleeping = true;
    asleep_ = earlier.AddTab(std::move(sleeping));
    ParseResult stored = ParseState(earlier.ToValue());
    ASSERT_EQ(stored.status, LoadStatus::kOk);
    model_->MergeLoaded(std::move(stored.state));
  }

  std::string folder_;
  std::string open_;
  std::string in_folder_;
  std::string asleep_;
};

TEST_F(TabModelRestoreJoinTest, ALoadedTodayTabIsClaimedByItsIdAndKeepsEverything) {
  StoreAndLoad();
  EXPECT_EQ(model_->ClaimRestoredTab(open_), open_);
  EXPECT_EQ(model_->ClaimRestoredTab(in_folder_), in_folder_);
  const TabRecord* named = model_->GetTab(open_);
  ASSERT_TRUE(named);
  EXPECT_EQ(named->manual_name, "My name");
  EXPECT_EQ(named->section, Section::kToday);
  EXPECT_EQ(named->position, 3);
  const TabRecord* filed = model_->GetTab(in_folder_);
  ASSERT_TRUE(filed);
  EXPECT_EQ(filed->parent, folder_);
  EXPECT_EQ(filed->position, 1);
  // Claimed records are not removed when the pass ends.
  EXPECT_EQ(model_->RemoveUnclaimedRestoredTabs(), 0u);
  EXPECT_TRUE(model_->GetTab(open_));
  EXPECT_TRUE(model_->GetTab(in_folder_));
  // The sleeping tab was never waiting: the Tab Model keeps it as before.
  EXPECT_TRUE(model_->GetTab(asleep_));
}

TEST_F(TabModelRestoreJoinTest, NoIdGivesNoClaimAndChangesNothing) {
  StoreAndLoad();
  const size_t tabs = model_->tabs().size();
  const int64_t next = model_->next_change();
  EXPECT_EQ(model_->ClaimRestoredTab(""), "");
  EXPECT_EQ(model_->tabs().size(), tabs);
  EXPECT_EQ(model_->next_change(), next);
  EXPECT_TRUE(model_->IsAwaitingRestore(open_));
}

TEST_F(TabModelRestoreJoinTest, AnUnknownIdGivesNoClaim) {
  StoreAndLoad();
  EXPECT_EQ(model_->ClaimRestoredTab("old-9999"), "");
  EXPECT_EQ(model_->ClaimRestoredTab("0f8fad5b-d9cb-469f-a165-70867728950e"), "");
  EXPECT_TRUE(model_->IsAwaitingRestore(open_));
}

TEST_F(TabModelRestoreJoinTest, AMalformedIdGivesNoClaim) {
  StoreAndLoad();
  EXPECT_EQ(model_->ClaimRestoredTab("../../etc"), "");
  EXPECT_EQ(model_->ClaimRestoredTab(std::string(100000, 'x')), "");
  EXPECT_EQ(model_->ClaimRestoredTab(std::string("old-1\0old-2", 11)), "");
  EXPECT_TRUE(model_->IsAwaitingRestore(open_));
}

TEST_F(TabModelRestoreJoinTest, OnlyAWaitingTodayRecordCanBeClaimed) {
  StoreAndLoad();
  // A sleeping record, a folder id and a record made in this run are not
  // claimable.
  EXPECT_EQ(model_->ClaimRestoredTab(asleep_), "");
  EXPECT_EQ(model_->ClaimRestoredTab(folder_), "");
  const std::string fresh = AddTab();
  EXPECT_EQ(model_->ClaimRestoredTab(fresh), "");
}

TEST_F(TabModelRestoreJoinTest, TwoTabsWithTheSameIdDoNotShareARecord) {
  StoreAndLoad();
  // Two windows that both carry the same id (a damaged file): the first claims,
  // the second gets a fresh record.
  EXPECT_EQ(model_->ClaimRestoredTab(open_), open_);
  EXPECT_EQ(model_->ClaimRestoredTab(open_), "");
}

TEST_F(TabModelRestoreJoinTest, TwoWindowsClaimTheirOwnRecordsWithoutCrossing) {
  StoreAndLoad();
  // Window one holds `open_`, window two holds `in_folder_`; they restore in
  // either order.
  EXPECT_EQ(model_->ClaimRestoredTab(in_folder_), in_folder_);
  EXPECT_EQ(model_->ClaimRestoredTab(open_), open_);
  EXPECT_EQ(model_->GetTab(open_)->manual_name, "My name");
  EXPECT_EQ(model_->GetTab(in_folder_)->parent, folder_);
  EXPECT_EQ(model_->GetTab(open_)->parent, "");
}

TEST_F(TabModelRestoreJoinTest, UnclaimedTodayTabsAreRemovedAtTheEndOfThePassButFoldersStay) {
  StoreAndLoad();
  EXPECT_EQ(model_->ClaimRestoredTab(open_), open_);
  EXPECT_EQ(model_->RemoveUnclaimedRestoredTabs(), 1u);
  EXPECT_EQ(model_->GetTab(in_folder_), nullptr);
  EXPECT_TRUE(model_->GetTab(open_));
  EXPECT_TRUE(model_->GetFolder(folder_));
  EXPECT_EQ(model_->change_log().back().type, ChangeType::kRemoved);
  EXPECT_EQ(model_->change_log().back().record_id, in_folder_);
  // The pass is over: nothing waits, and a second sweep does nothing.
  EXPECT_FALSE(model_->IsAwaitingRestore(in_folder_));
  EXPECT_EQ(model_->RemoveUnclaimedRestoredTabs(), 0u);
  // A late claim finds nothing.
  EXPECT_EQ(model_->ClaimRestoredTab(in_folder_), "");
}

TEST_F(TabModelRestoreJoinTest, ATabInsertedBeforeTheReadFinishedSwapsItsFreshRecordForTheStoredOne) {
  // The tab was inserted, and got a fresh record, before the read was merged.
  const std::string fresh = AddTab();
  StoreAndLoad();
  EXPECT_EQ(model_->ClaimRestoredTabInPlaceOf(fresh, open_), open_);
  EXPECT_EQ(model_->GetTab(fresh), nullptr);
  EXPECT_EQ(model_->GetTab(open_)->manual_name, "My name");
}

TEST_F(TabModelRestoreJoinTest, TheSwapKeepsTheFreshRecordWhenThereIsNothingToClaim) {
  const std::string fresh = AddTab();
  StoreAndLoad();
  EXPECT_EQ(model_->ClaimRestoredTabInPlaceOf(fresh, "old-9999"), "");
  EXPECT_EQ(model_->ClaimRestoredTabInPlaceOf(fresh, ""), "");
  EXPECT_TRUE(model_->GetTab(fresh));
}

TEST_F(TabModelRestoreJoinTest, TheSwapNeverRemovesARecordThatIsNotFresh) {
  StoreAndLoad();
  // `asleep_` is a stored, Today, sleeping record: a swap that names it as the
  // fresh one must not delete it.
  EXPECT_EQ(model_->ClaimRestoredTabInPlaceOf(asleep_, open_), "");
  EXPECT_TRUE(model_->GetTab(asleep_));
  EXPECT_TRUE(model_->IsAwaitingRestore(open_));
  EXPECT_EQ(model_->ClaimRestoredTabInPlaceOf("old-9999", open_), "");
  EXPECT_TRUE(model_->IsAwaitingRestore(open_));
}

TEST_F(TabModelRestoreJoinTest, ALiveAwakeTodayRecordCanBeMadeToWaitAndThenBeClaimedOrSwept) {
  model_->MergeLoaded(std::nullopt);
  const std::string kept = AddTab();
  const std::string swept = AddTab();
  EXPECT_TRUE(model_->MarkAwaitingRestore(kept));
  EXPECT_TRUE(model_->MarkAwaitingRestore(swept));
  EXPECT_EQ(model_->ClaimRestoredTab(kept), kept);
  EXPECT_EQ(model_->RemoveUnclaimedRestoredTabs(), 1u);
  EXPECT_TRUE(model_->GetTab(kept));
  EXPECT_EQ(model_->GetTab(swept), nullptr);
}

TEST_F(TabModelRestoreJoinTest, OnlyAnAwakeTodayRecordCanBeMadeToWait) {
  model_->MergeLoaded(std::nullopt);
  const std::string pinned = AddTab(Section::kPinned);
  const std::string asleep = AddTab();
  ASSERT_TRUE(model_->UpdateTab(asleep, [](TabRecord& t) { t.sleeping = true; }));
  EXPECT_FALSE(model_->MarkAwaitingRestore(pinned));
  EXPECT_FALSE(model_->MarkAwaitingRestore(asleep));
  EXPECT_FALSE(model_->MarkAwaitingRestore("no-such-id"));
  EXPECT_EQ(model_->awaiting_restore_count(), 0u);
}

TEST_F(TabModelRestoreJoinTest, NothingWaitsWhenNothingIsStored) {
  model_->MergeLoaded(std::nullopt);
  const std::string tab = AddTab();
  EXPECT_FALSE(model_->IsAwaitingRestore(tab));
  EXPECT_EQ(model_->ClaimRestoredTab(tab), "");
  EXPECT_EQ(model_->RemoveUnclaimedRestoredTabs(), 0u);
  EXPECT_TRUE(model_->GetTab(tab));
}

// ParseState.

base::DictValue ValidState() {
  TabModel model(base::DefaultClock::GetInstance());
  model.MergeLoaded(std::nullopt);
  return model.ToValue();
}

TEST(TabModelParseTest, ReadsWhatTheModelWrites) {
  EXPECT_EQ(ParseState(ValidState()).status, LoadStatus::kOk);
}

TEST(TabModelParseTest, ANewerSchemaIsReportedNotRead) {
  base::DictValue state = ValidState();
  state.Set("version", kSchemaVersion + 1);
  EXPECT_EQ(ParseState(state).status, LoadStatus::kNewerSchema);
}

TEST(TabModelParseTest, ABadVersionIsCorrupt) {
  base::DictValue state = ValidState();
  state.Remove("version");
  EXPECT_EQ(ParseState(state).status, LoadStatus::kCorrupt);
  state.Set("version", 0);
  EXPECT_EQ(ParseState(state).status, LoadStatus::kCorrupt);
}

TEST(TabModelParseTest, AMissingPartIsCorrupt) {
  for (const char* key : {"workspace", "tabs", "folders", "changes", "next_change"}) {
    base::DictValue state = ValidState();
    state.Remove(key);
    EXPECT_EQ(ParseState(state).status, LoadStatus::kCorrupt) << key;
  }
}

TEST(TabModelParseTest, ARecordWithAWrongFieldTypeIsCorrupt) {
  base::DictValue state = ValidState();
  base::DictValue tab;
  tab.Set("id", 5);
  state.FindList("tabs")->Append(std::move(tab));
  EXPECT_EQ(ParseState(state).status, LoadStatus::kCorrupt);
}

TEST(TabModelParseTest, AnIdUsedTwiceIsCorrupt) {
  TabModel model(base::DefaultClock::GetInstance());
  model.MergeLoaded(std::nullopt);
  model.AddTab(TabRecord());
  base::DictValue state = model.ToValue();
  base::ListValue* tabs = state.FindList("tabs");
  tabs->Append((*tabs)[0].Clone());
  EXPECT_EQ(ParseState(state).status, LoadStatus::kCorrupt);
}

TEST(TabModelParseTest, ACounterBeyondTheNextOneIsCorrupt) {
  base::DictValue state = ValidState();
  state.Set("next_change", base::Int64ToValue(1));
  EXPECT_EQ(ParseState(state).status, LoadStatus::kCorrupt);
}

void AddGreeting(base::DictValue& state) {
  state.Set("greeting", "v2");
}
void RenameGreeting(base::DictValue& state) {
  state.Set("greeting", "v3");
}

TEST(TabModelMigrationTest, StepsRunInOrderFromTheStoredVersion) {
  static constexpr MigrationStep kSteps[] = {&AddGreeting, &RenameGreeting};
  base::DictValue state;
  state.Set("version", 1);
  EXPECT_EQ(MigrateToVersion(state, kSteps, 3), LoadStatus::kOk);
  EXPECT_EQ(*state.FindString("greeting"), "v3");
  EXPECT_EQ(state.FindInt("version"), 3);

  base::DictValue from_two;
  from_two.Set("version", 2);
  EXPECT_EQ(MigrateToVersion(from_two, kSteps, 3), LoadStatus::kOk);
  EXPECT_EQ(*from_two.FindString("greeting"), "v3");
}

TEST(TabModelMigrationTest, ACurrentFileRunsNoStep) {
  static constexpr MigrationStep kSteps[] = {&AddGreeting};
  base::DictValue state;
  state.Set("version", 2);
  EXPECT_EQ(MigrateToVersion(state, kSteps, 2), LoadStatus::kOk);
  EXPECT_EQ(state.FindString("greeting"), nullptr);
}

TEST(TabModelMigrationTest, ANewerFileIsRefused) {
  base::DictValue state;
  state.Set("version", 4);
  EXPECT_EQ(MigrateToVersion(state, std::span<const MigrationStep>(), 1), LoadStatus::kNewerSchema);
}

// previous_section (issue #180).
TEST_F(TabModelTest, ATabThatEntersFavoritesRemembersItsSectionAndForgetsItOnLeaving) {
  const std::string id = AddTab(Section::kPinned);
  EXPECT_FALSE(model_->GetTab(id)->previous_section.has_value());
  ASSERT_TRUE(model_->MoveTab(id, Section::kFavorite, std::string(), 0));
  EXPECT_EQ(model_->GetTab(id)->previous_section, Section::kPinned);
  // A move inside Favorites keeps it.
  ASSERT_TRUE(model_->MoveTab(id, Section::kFavorite, std::string(), 4));
  EXPECT_EQ(model_->GetTab(id)->previous_section, Section::kPinned);
  ASSERT_TRUE(model_->MoveTab(id, Section::kToday, std::string(), 0));
  EXPECT_FALSE(model_->GetTab(id)->previous_section.has_value());
}

TEST_F(TabModelTest, PreviousSectionSurvivesTheRoundTripAndAnOldFileWithoutItLoads) {
  const std::string id = AddTab(Section::kToday);
  ASSERT_TRUE(model_->MoveTab(id, Section::kFavorite, std::string(), 0));
  base::DictValue json = model_->ToValue();
  ParseResult parsed = ParseState(json);
  ASSERT_EQ(parsed.status, LoadStatus::kOk);
  EXPECT_EQ(parsed.state->tabs[0].previous_section, Section::kToday);

  model_->UpdateTab(id, [](TabRecord& tab) { tab.last_title = "Inbox"; });
  parsed = ParseState(model_->ToValue());
  ASSERT_EQ(parsed.status, LoadStatus::kOk);
  EXPECT_EQ(parsed.state->tabs[0].last_title, "Inbox");

  // A file from before the field: no key, still valid, and the field is empty.
  base::DictValue old = model_->ToValue();
  old.FindList("tabs")->front().GetDict().Remove("previous_section");
  parsed = ParseState(old);
  ASSERT_EQ(parsed.status, LoadStatus::kOk);
  EXPECT_FALSE(parsed.state->tabs[0].previous_section.has_value());

  // A value that is not a section is a corrupt file.
  base::DictValue bad = model_->ToValue();
  bad.FindList("tabs")->front().GetDict().Set("previous_section", "elsewhere");
  EXPECT_EQ(ParseState(bad).status, LoadStatus::kCorrupt);
}

}  // namespace

}  // namespace aerie_tab_model
