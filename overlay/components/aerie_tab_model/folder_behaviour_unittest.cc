// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_tab_model/folder_behaviour.h"

#include <string>

#include "base/test/task_environment.h"
#include "base/values.h"
#include "components/aerie_tab_model/folder_rules.h"
#include "components/aerie_tab_model/folder_tree.h"
#include "components/aerie_tab_model/tab_model.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace aerie_tab_model {

namespace {

class FolderBehaviourTest : public testing::Test {
 protected:
  FolderBehaviourTest() : model_(task_environment_.GetMockClock()) {
    global_.archive_after = base::Hours(12);
    global_.sleep_after = base::Hours(1);
    global_.links = LinksOpen::kNewTab;
  }

  std::string Folder(Section section, const std::string& parent = "") {
    return CreateFolder(model_, section, parent, "F");
  }

  std::string Tab(Section section, const std::string& parent = "") {
    TabRecord tab;
    tab.section = section;
    tab.parent = parent;
    return model_.AddTab(std::move(tab));
  }

  const TabRecord& Rec(const std::string& id) { return *model_.GetTab(id); }

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  TabModel model_;
  GlobalBehaviour global_;
};

TEST_F(FolderBehaviourTest, ATabWithNoFolderGetsTheGlobalValues) {
  const std::string tab = Tab(Section::kToday);
  EXPECT_EQ(EffectiveArchiveAfter(model_, Rec(tab), global_), base::Hours(12));
  EXPECT_EQ(EffectiveSleepAfter(model_, Rec(tab), global_), base::Hours(1));
  EXPECT_EQ(EffectiveLinksOpen(model_, Rec(tab), global_), LinksOpen::kNewTab);
}

TEST_F(FolderBehaviourTest, AFolderThatSetsNothingInheritsTheGlobalValues) {
  const std::string folder = Folder(Section::kToday);
  const std::string tab = Tab(Section::kToday, folder);
  const auto archive = ResolveArchiveAfter(model_, Rec(tab), global_);
  EXPECT_EQ(archive.value, base::Hours(12));
  EXPECT_EQ(archive.source_folder, "");
  EXPECT_EQ(EffectiveSleepAfter(model_, Rec(tab), global_), base::Hours(1));
}

TEST_F(FolderBehaviourTest, AFolderOwnValueBeatsTheGlobalOne) {
  const std::string folder = Folder(Section::kToday);
  const std::string tab = Tab(Section::kToday, folder);
  ASSERT_TRUE(SetFolderArchiveAfter(model_, folder, ArchiveChoice::k7Days));
  ASSERT_TRUE(SetFolderSleepAfter(model_, folder, 15));
  ASSERT_TRUE(SetFolderLinksOpen(model_, folder, LinksOpen::kInPlace));
  const auto archive = ResolveArchiveAfter(model_, Rec(tab), global_);
  EXPECT_EQ(archive.value, base::Days(7));
  EXPECT_EQ(archive.source_folder, folder);
  EXPECT_EQ(EffectiveSleepAfter(model_, Rec(tab), global_), base::Minutes(15));
  EXPECT_EQ(EffectiveLinksOpen(model_, Rec(tab), global_), LinksOpen::kInPlace);
}

TEST_F(FolderBehaviourTest, NeverIsAValueOfItsOwnNotInheritance) {
  const std::string folder = Folder(Section::kToday);
  const std::string tab = Tab(Section::kToday, folder);
  ASSERT_TRUE(SetFolderArchiveAfter(model_, folder, ArchiveChoice::kNever));
  ASSERT_TRUE(SetFolderSleepAfter(model_, folder, 0));
  EXPECT_EQ(EffectiveArchiveAfter(model_, Rec(tab), global_), std::nullopt);
  EXPECT_EQ(EffectiveSleepAfter(model_, Rec(tab), global_), std::nullopt);
}

TEST_F(FolderBehaviourTest, TheNearestAncestorThatSetsTheBehaviourWins) {
  const std::string outer = Folder(Section::kToday);
  const std::string middle = Folder(Section::kToday, outer);
  const std::string inner = Folder(Section::kToday, middle);
  const std::string tab = Tab(Section::kToday, inner);
  ASSERT_TRUE(SetFolderArchiveAfter(model_, outer, ArchiveChoice::kNever));
  // The middle folder sets nothing: the outer one applies.
  EXPECT_EQ(ResolveArchiveAfter(model_, Rec(tab), global_).source_folder, outer);
  EXPECT_EQ(EffectiveArchiveAfter(model_, Rec(tab), global_), std::nullopt);
  ASSERT_TRUE(SetFolderArchiveAfter(model_, middle, ArchiveChoice::k24Hours));
  EXPECT_EQ(ResolveArchiveAfter(model_, Rec(tab), global_).source_folder,
            middle);
  EXPECT_EQ(EffectiveArchiveAfter(model_, Rec(tab), global_), base::Hours(24));
  ASSERT_TRUE(SetFolderArchiveAfter(model_, inner, ArchiveChoice::k12Hours));
  EXPECT_EQ(EffectiveArchiveAfter(model_, Rec(tab), global_), base::Hours(12));
  EXPECT_EQ(ResolveArchiveAfter(model_, Rec(tab), global_).source_folder,
            inner);
}

TEST_F(FolderBehaviourTest, EachBehaviourIsResolvedOnItsOwn) {
  const std::string outer = Folder(Section::kToday);
  const std::string inner = Folder(Section::kToday, outer);
  const std::string tab = Tab(Section::kToday, inner);
  ASSERT_TRUE(SetFolderLinksOpen(model_, outer, LinksOpen::kInPlace));
  ASSERT_TRUE(SetFolderSleepAfter(model_, inner, 120));
  EXPECT_EQ(EffectiveLinksOpen(model_, Rec(tab), global_), LinksOpen::kInPlace);
  EXPECT_EQ(EffectiveSleepAfter(model_, Rec(tab), global_), base::Hours(2));
  // The archive timeout is set nowhere: the global one.
  EXPECT_EQ(EffectiveArchiveAfter(model_, Rec(tab), global_), base::Hours(12));
}

TEST_F(FolderBehaviourTest, ATabInAPinnedFolderNeverArchivesWhateverIsSet) {
  const std::string pinned = Folder(Section::kPinned);
  const std::string tab = Tab(Section::kPinned, pinned);
  EXPECT_EQ(EffectiveArchiveAfter(model_, Rec(tab), global_), std::nullopt);
  // A damaged record that holds an archive timeout does not change that.
  model_.UpdateFolder(pinned, [](FolderRecord& folder) {
    base::DictValue dict;
    dict.Set("archive_after", "12h");
    folder.behaviour = std::move(dict);
  });
  EXPECT_EQ(EffectiveArchiveAfter(model_, Rec(tab), global_), std::nullopt);
  // Sleep and links are not tied to Pinned.
  EXPECT_EQ(EffectiveSleepAfter(model_, Rec(tab), global_), base::Hours(1));
}

TEST_F(FolderBehaviourTest, APinnedTabNeverArchivesEvenWithNoFolder) {
  const std::string tab = Tab(Section::kPinned);
  EXPECT_EQ(EffectiveArchiveAfter(model_, Rec(tab), global_), std::nullopt);
}

TEST_F(FolderBehaviourTest, ThePinnedFolderSettingIsRefusedForArchiveOnly) {
  const std::string pinned = Folder(Section::kPinned);
  EXPECT_FALSE(SetFolderArchiveAfter(model_, pinned, ArchiveChoice::k7Days));
  EXPECT_TRUE(SetFolderSleepAfter(model_, pinned, 30));
  EXPECT_TRUE(SetFolderLinksOpen(model_, pinned, LinksOpen::kInPlace));
}

TEST_F(FolderBehaviourTest, AGlobalChangeFlowsThroughInheritance) {
  const std::string folder = Folder(Section::kToday);
  const std::string tab = Tab(Section::kToday, folder);
  ASSERT_TRUE(SetFolderSleepAfter(model_, folder, 15));
  global_.archive_after = base::Days(7);
  global_.sleep_after = base::Hours(8);
  global_.links = LinksOpen::kInPlace;
  // Archive and links are inherited, so they follow; sleep is the folder's own.
  EXPECT_EQ(EffectiveArchiveAfter(model_, Rec(tab), global_), base::Days(7));
  EXPECT_EQ(EffectiveLinksOpen(model_, Rec(tab), global_), LinksOpen::kInPlace);
  EXPECT_EQ(EffectiveSleepAfter(model_, Rec(tab), global_), base::Minutes(15));
  global_.archive_after = std::nullopt;
  EXPECT_EQ(EffectiveArchiveAfter(model_, Rec(tab), global_), std::nullopt);
}

TEST_F(FolderBehaviourTest, ClearingASettingGoesBackToInheriting) {
  const std::string folder = Folder(Section::kToday);
  const std::string tab = Tab(Section::kToday, folder);
  ASSERT_TRUE(SetFolderArchiveAfter(model_, folder, ArchiveChoice::kNever));
  ASSERT_TRUE(SetFolderArchiveAfter(model_, folder, std::nullopt));
  EXPECT_EQ(EffectiveArchiveAfter(model_, Rec(tab), global_), base::Hours(12));
  // Nothing is stored when nothing is set.
  EXPECT_FALSE(model_.GetFolder(folder)->behaviour);
  EXPECT_FALSE(SetFolderArchiveAfter(model_, folder, std::nullopt));
}

TEST_F(FolderBehaviourTest, AnUnchangedValueWritesNothing) {
  const std::string folder = Folder(Section::kToday);
  ASSERT_TRUE(SetFolderLinksOpen(model_, folder, LinksOpen::kInPlace));
  const int64_t change = model_.GetFolder(folder)->change;
  EXPECT_FALSE(SetFolderLinksOpen(model_, folder, LinksOpen::kInPlace));
  EXPECT_EQ(model_.GetFolder(folder)->change, change);
}

TEST_F(FolderBehaviourTest, SleepMinutesAreClampedToTheGlobalRange) {
  const std::string folder = Folder(Section::kToday);
  ASSERT_TRUE(SetFolderSleepAfter(model_, folder, 5));
  EXPECT_EQ(ReadFolderBehaviour(*model_.GetFolder(folder)).sleep_after_minutes,
            15);
  ASSERT_TRUE(SetFolderSleepAfter(model_, folder, 5000));
  EXPECT_EQ(ReadFolderBehaviour(*model_.GetFolder(folder)).sleep_after_minutes,
            480);
  ASSERT_TRUE(SetFolderSleepAfter(model_, folder, 0));
  EXPECT_EQ(ReadFolderBehaviour(*model_.GetFolder(folder)).sleep_after_minutes,
            0);
  // A negative number clears the setting.
  ASSERT_TRUE(SetFolderSleepAfter(model_, folder, -1));
  EXPECT_FALSE(
      ReadFolderBehaviour(*model_.GetFolder(folder)).sleep_after_minutes);
}

TEST_F(FolderBehaviourTest, ASmartFolderHasNoBehaviour) {
  const std::string folder = Folder(Section::kToday);
  ASSERT_TRUE(SetFolderLinksOpen(model_, folder, LinksOpen::kInPlace));
  ASSERT_TRUE(SetFolderRuleKind(model_, folder, FolderKind::kSmart));
  EXPECT_EQ(ReadFolderBehaviour(*model_.GetFolder(folder)), FolderBehaviour());
  EXPECT_FALSE(SetFolderLinksOpen(model_, folder, LinksOpen::kNewTab));
  EXPECT_FALSE(SetFolderSleepAfter(model_, folder, 30));
}

TEST_F(FolderBehaviourTest, DamagedValuesReadAsInherits) {
  const std::string folder = Folder(Section::kToday);
  model_.UpdateFolder(folder, [](FolderRecord& record) {
    base::DictValue dict;
    dict.Set("archive_after", "tomorrow");
    dict.Set("sleep_after_minutes", "soon");
    dict.Set("links", 3);
    record.behaviour = std::move(dict);
  });
  EXPECT_EQ(ReadFolderBehaviour(*model_.GetFolder(folder)), FolderBehaviour());
}

TEST_F(FolderBehaviourTest, TheValuesSurviveTheStateFile) {
  const std::string folder = Folder(Section::kToday);
  ASSERT_TRUE(SetFolderArchiveAfter(model_, folder, ArchiveChoice::k24Hours));
  ASSERT_TRUE(SetFolderSleepAfter(model_, folder, 90));
  ASSERT_TRUE(SetFolderLinksOpen(model_, folder, LinksOpen::kInPlace));
  ParseResult parsed = ParseState(model_.ToValue());
  ASSERT_EQ(parsed.status, LoadStatus::kOk);
  ASSERT_EQ(parsed.state->folders.size(), 1u);
  FolderBehaviour behaviour = ReadFolderBehaviour(parsed.state->folders[0]);
  EXPECT_EQ(behaviour.archive_after, ArchiveChoice::k24Hours);
  EXPECT_EQ(behaviour.sleep_after_minutes, 90);
  EXPECT_EQ(behaviour.links, LinksOpen::kInPlace);
}

// Issue #267: a kept tab never sleeps and never archives. The flag on the tab
// beats every folder rule and the global setting.
TEST_F(FolderBehaviourTest, AKeptTabNeverSleepsOrArchivesAndItsNeighbourDoes) {
  const std::string kept = Tab(Section::kToday);
  const std::string neighbour = Tab(Section::kToday);
  ASSERT_TRUE(SetTabKeepLoaded(model_, kept, true));
  EXPECT_EQ(EffectiveSleepAfter(model_, Rec(kept), global_), std::nullopt);
  EXPECT_EQ(EffectiveArchiveAfter(model_, Rec(kept), global_), std::nullopt);
  EXPECT_EQ(EffectiveSleepAfter(model_, Rec(neighbour), global_),
            base::Hours(1));
  EXPECT_EQ(EffectiveArchiveAfter(model_, Rec(neighbour), global_),
            base::Hours(12));
}

TEST_F(FolderBehaviourTest, AKeptTabInAFolderWithRulesIsStillSkipped) {
  const std::string folder = Folder(Section::kToday);
  ASSERT_TRUE(SetFolderArchiveAfter(model_, folder, ArchiveChoice::k24Hours));
  ASSERT_TRUE(SetFolderSleepAfter(model_, folder, 15));
  const std::string kept = Tab(Section::kToday, folder);
  const std::string plain = Tab(Section::kToday, folder);
  ASSERT_TRUE(SetTabKeepLoaded(model_, kept, true));
  EXPECT_EQ(EffectiveSleepAfter(model_, Rec(kept), global_), std::nullopt);
  EXPECT_EQ(EffectiveArchiveAfter(model_, Rec(kept), global_), std::nullopt);
  EXPECT_EQ(EffectiveSleepAfter(model_, Rec(plain), global_),
            base::Minutes(15));
  EXPECT_EQ(EffectiveArchiveAfter(model_, Rec(plain), global_), base::Days(1));
}

TEST_F(FolderBehaviourTest, ClearingKeepLoadedGivesTheRulesBack) {
  const std::string tab = Tab(Section::kToday);
  EXPECT_FALSE(SetTabKeepLoaded(model_, tab, false));  // Already off.
  ASSERT_TRUE(SetTabKeepLoaded(model_, tab, true));
  EXPECT_FALSE(SetTabKeepLoaded(model_, tab, true));  // Changes nothing.
  ASSERT_TRUE(SetTabKeepLoaded(model_, tab, false));
  EXPECT_EQ(EffectiveSleepAfter(model_, Rec(tab), global_), base::Hours(1));
  EXPECT_FALSE(SetTabKeepLoaded(model_, "no-such-tab", true));
}

TEST_F(FolderBehaviourTest, AFolderShowsWhatItWouldInherit) {
  const std::string outer = Folder(Section::kToday);
  const std::string inner = Folder(Section::kToday, outer);
  // Nothing above sets it: the global value.
  auto inherited =
      InheritedLinksOpen(model_, *model_.GetFolder(inner), global_);
  EXPECT_EQ(inherited.value, LinksOpen::kNewTab);
  EXPECT_EQ(inherited.source_folder, "");
  // The folder's own setting is not what it inherits.
  ASSERT_TRUE(SetFolderLinksOpen(model_, inner, LinksOpen::kInPlace));
  EXPECT_EQ(
      InheritedLinksOpen(model_, *model_.GetFolder(inner), global_).value,
      LinksOpen::kNewTab);
  // A parent that sets it is named.
  ASSERT_TRUE(SetFolderLinksOpen(model_, outer, LinksOpen::kInPlace));
  inherited = InheritedLinksOpen(model_, *model_.GetFolder(inner), global_);
  EXPECT_EQ(inherited.value, LinksOpen::kInPlace);
  EXPECT_EQ(inherited.source_folder, outer);
  // A Pinned folder inherits "never" for archive.
  const std::string pinned = Folder(Section::kPinned);
  EXPECT_EQ(
      InheritedArchiveAfter(model_, *model_.GetFolder(pinned), global_).value,
      std::nullopt);
}

}  // namespace

}  // namespace aerie_tab_model
