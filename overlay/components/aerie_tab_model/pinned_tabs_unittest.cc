// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_tab_model/pinned_tabs.h"

#include <string>
#include <vector>

#include "base/test/simple_test_clock.h"
#include "components/aerie_tab_model/tab_model.h"
#include "components/aerie_tab_model/tab_model_prefs.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace aerie_tab_model {

namespace {

class PinnedTabsTest : public testing::Test {
 protected:
  PinnedTabsTest() : model_(&clock_) {
    clock_.SetNow(base::Time::FromMillisecondsSinceUnixEpoch(1'700'000'000'000));
  }

  std::string Add(Section section, int position) {
    TabRecord tab;
    tab.section = section;
    tab.position = position;
    return model_.AddTab(std::move(tab));
  }

  base::SimpleTestClock clock_;
  TabModel model_;
};

}  // namespace

TEST_F(PinnedTabsTest, SectionTabIdsListsOneSectionInPositionOrder) {
  const std::string b = Add(Section::kPinned, 2);
  const std::string a = Add(Section::kPinned, 1);
  Add(Section::kToday, 0);
  EXPECT_EQ(SectionTabIds(model_, Section::kPinned),
            (std::vector<std::string>{a, b}));
}

TEST_F(PinnedTabsTest, TheSidebarOrderIsFavoritesThenPinnedThenToday) {
  const std::string today = Add(Section::kToday, 0);
  const std::string pinned = Add(Section::kPinned, 0);
  const std::string favorite = Add(Section::kFavorite, 0);
  EXPECT_EQ(SidebarTabOrder(model_),
            (std::vector<std::string>{favorite, pinned, today}));
}

TEST(PickNextTabTest, TheRowBelowThenTheRowAboveThenNothing) {
  const std::vector<std::string> order = {"a", "b", "c"};
  EXPECT_EQ(PickNextTab(order, "a"), "b");
  EXPECT_EQ(PickNextTab(order, "b"), "c");
  EXPECT_EQ(PickNextTab(order, "c"), "b");
  EXPECT_EQ(PickNextTab({"only"}, "only"), "");
  EXPECT_EQ(PickNextTab(order, "missing"), "");
}

TEST_F(PinnedTabsTest, PinMovesTheTabToTheEndOfPinnedAndKeepsItsUrl) {
  const std::string first = Add(Section::kToday, 0);
  const std::string second = Add(Section::kToday, 1);
  ASSERT_TRUE(PinTab(model_, first, "https://a.example/"));
  ASSERT_TRUE(PinTab(model_, second, "https://b.example/x"));
  EXPECT_EQ(SectionTabIds(model_, Section::kPinned),
            (std::vector<std::string>{first, second}));
  EXPECT_EQ(model_.GetTab(second)->section, Section::kPinned);
  EXPECT_EQ(model_.GetTab(second)->pinned_url, "https://b.example/x");
  EXPECT_TRUE(SectionTabIds(model_, Section::kToday).empty());
  EXPECT_FALSE(PinTab(model_, "missing", "https://c.example/"));
}

// MoveTab runs first and the pinned URL is set only after it succeeds, so a
// refused pin or unpin leaves the record and the change counter alone. (A move
// to the top level cannot be refused for a tab that exists, so the refusal that
// can be made is the unknown tab.)
TEST_F(PinnedTabsTest, APinOrUnpinThatDoesNotMoveTheTabChangesNothing) {
  const std::string tab = Add(Section::kToday, 0);
  const int64_t change_before = model_.next_change();
  EXPECT_FALSE(PinTab(model_, "missing", "https://a.example/"));
  EXPECT_FALSE(UnpinTab(model_, "missing"));
  EXPECT_EQ(model_.next_change(), change_before);
  EXPECT_FALSE(model_.GetTab(tab)->pinned_url.has_value());
  EXPECT_EQ(model_.GetTab(tab)->section, Section::kToday);
  // Unpinning a tab that was never pinned leaves no URL behind.
  ASSERT_TRUE(UnpinTab(model_, tab));
  EXPECT_FALSE(model_.GetTab(tab)->pinned_url.has_value());
}

TEST_F(PinnedTabsTest, UnpinPutsTheTabAtTheTopOfTodayAndForgetsTheUrl) {
  const std::string today = Add(Section::kToday, 3);
  const std::string pinned = Add(Section::kPinned, 0);
  model_.UpdateTab(pinned, [](TabRecord& tab) { tab.pinned_url = "https://a.example/"; });
  ASSERT_TRUE(UnpinTab(model_, pinned));
  EXPECT_EQ(SectionTabIds(model_, Section::kToday),
            (std::vector<std::string>{pinned, today}));
  EXPECT_FALSE(model_.GetTab(pinned)->pinned_url.has_value());
}

TEST_F(PinnedTabsTest, AddToFavoritesRemembersWhereTheTabWas) {
  const std::string today = Add(Section::kToday, 0);
  ASSERT_EQ(AddToFavorites(model_, today, "https://a.example/"),
            FavoriteResult::kAdded);
  const TabRecord* tab = model_.GetTab(today);
  EXPECT_EQ(tab->section, Section::kFavorite);
  EXPECT_EQ(tab->previous_section, Section::kToday);
  EXPECT_EQ(tab->pinned_url, "https://a.example/");
}

TEST_F(PinnedTabsTest, RemovingAFavoriteReturnsItToWhereItWas) {
  const std::string from_today = Add(Section::kToday, 0);
  const std::string from_pinned = Add(Section::kPinned, 0);
  model_.UpdateTab(from_pinned, [](TabRecord& tab) { tab.pinned_url = "https://p.example/"; });
  ASSERT_EQ(AddToFavorites(model_, from_today, "https://t.example/"),
            FavoriteResult::kAdded);
  ASSERT_EQ(AddToFavorites(model_, from_pinned, "https://other.example/"),
            FavoriteResult::kAdded);
  // A pinned tab keeps the pinned URL it had.
  EXPECT_EQ(model_.GetTab(from_pinned)->pinned_url, "https://p.example/");

  ASSERT_TRUE(RemoveFromFavorites(model_, from_pinned));
  EXPECT_EQ(model_.GetTab(from_pinned)->section, Section::kPinned);
  EXPECT_EQ(model_.GetTab(from_pinned)->pinned_url, "https://p.example/");
  EXPECT_FALSE(model_.GetTab(from_pinned)->previous_section.has_value());

  ASSERT_TRUE(RemoveFromFavorites(model_, from_today));
  EXPECT_EQ(model_.GetTab(from_today)->section, Section::kToday);
  EXPECT_FALSE(model_.GetTab(from_today)->pinned_url.has_value());
  EXPECT_FALSE(RemoveFromFavorites(model_, from_today));
}

TEST_F(PinnedTabsTest, TheThirteenthFavoriteIsRefusedAndNothingChanges) {
  for (size_t i = 0; i < kFavoritesCap; ++i) {
    const std::string id = Add(Section::kToday, static_cast<int>(i));
    ASSERT_EQ(AddToFavorites(model_, id, "https://a.example/"),
              FavoriteResult::kAdded);
  }
  const std::string extra = Add(Section::kToday, 99);
  const int64_t change_before = model_.next_change();
  EXPECT_EQ(AddToFavorites(model_, extra, "https://a.example/"),
            FavoriteResult::kFull);
  EXPECT_EQ(model_.GetTab(extra)->section, Section::kToday);
  EXPECT_FALSE(model_.GetTab(extra)->pinned_url.has_value());
  EXPECT_EQ(model_.next_change(), change_before);
  EXPECT_EQ(SectionTabIds(model_, Section::kFavorite).size(), kFavoritesCap);
  EXPECT_EQ(AddToFavorites(model_, "missing", "https://a.example/"),
            FavoriteResult::kUnknownTab);
}

TEST(IsAtPinnedUrlTest, ComparesOriginAndPathAndIgnoresQueryAndFragment) {
  const GURL pinned("https://mail.example/inbox");
  EXPECT_TRUE(IsAtPinnedUrl(GURL("https://mail.example/inbox"), pinned));
  EXPECT_TRUE(IsAtPinnedUrl(GURL("https://mail.example/inbox#thread-4"), pinned));
  EXPECT_TRUE(IsAtPinnedUrl(GURL("https://mail.example/inbox?utm=1"), pinned));
  EXPECT_FALSE(IsAtPinnedUrl(GURL("https://mail.example/sent"), pinned));
  EXPECT_FALSE(IsAtPinnedUrl(GURL("https://other.example/inbox"), pinned));
  EXPECT_FALSE(IsAtPinnedUrl(GURL("http://mail.example/inbox"), pinned));
  EXPECT_FALSE(IsAtPinnedUrl(GURL("https://mail.example:8443/inbox"), pinned));
}

TEST(IsAtPinnedUrlTest, ATabThatHasNotLoadedIsAtItsPinnedPage) {
  EXPECT_TRUE(IsAtPinnedUrl(GURL(), GURL("https://mail.example/")));
  EXPECT_TRUE(IsAtPinnedUrl(GURL("https://mail.example/"), GURL()));
}

TEST(PinnedCloseTest, TheSettingHasThreeValuesAndTheDefaultIsResetSleepSwitch) {
  EXPECT_EQ(ParsePinnedCloseMode(kPinnedCloseResetSleepSwitch),
            PinnedCloseMode::kResetSleepSwitch);
  EXPECT_EQ(ParsePinnedCloseMode(kPinnedCloseSleep), PinnedCloseMode::kSleep);
  EXPECT_EQ(ParsePinnedCloseMode(kPinnedCloseClose), PinnedCloseMode::kClose);
  EXPECT_EQ(ParsePinnedCloseMode(""), PinnedCloseMode::kResetSleepSwitch);
  EXPECT_EQ(ParsePinnedCloseMode("nonsense"), PinnedCloseMode::kResetSleepSwitch);
  EXPECT_STREQ(kDefaultPinnedClose, kPinnedCloseResetSleepSwitch);
}

TEST(PinnedCloseTest, ResetSleepSwitchFollowsTheTable) {
  const auto mode = PinnedCloseMode::kResetSleepSwitch;
  // Away from the pinned page, asleep or not: go back to it first.
  EXPECT_EQ(DecidePinnedClose(mode, false, false), PinnedCloseAction::kResetSleepSwitch);
  EXPECT_EQ(DecidePinnedClose(mode, true, false), PinnedCloseAction::kResetSleepSwitch);
  // At the pinned page and awake: nothing to reset, so it only sleeps.
  EXPECT_EQ(DecidePinnedClose(mode, false, true), PinnedCloseAction::kSleepSwitch);
  // Second press: asleep and at the pinned URL.
  EXPECT_EQ(DecidePinnedClose(mode, true, true), PinnedCloseAction::kUnpinClose);
}

TEST(PinnedCloseTest, SleepOnlyClosesOnTheSecondPressWhateverTheUrlIs) {
  const auto mode = PinnedCloseMode::kSleep;
  EXPECT_EQ(DecidePinnedClose(mode, false, true), PinnedCloseAction::kSleepSwitch);
  EXPECT_EQ(DecidePinnedClose(mode, false, false), PinnedCloseAction::kSleepSwitch);
  EXPECT_EQ(DecidePinnedClose(mode, true, true), PinnedCloseAction::kUnpinClose);
  EXPECT_EQ(DecidePinnedClose(mode, true, false), PinnedCloseAction::kUnpinClose);
}

TEST(PinnedCloseTest, CloseAlwaysUnpinsAndCloses) {
  for (bool asleep : {false, true}) {
    for (bool at_pinned : {false, true}) {
      EXPECT_EQ(DecidePinnedClose(PinnedCloseMode::kClose, asleep, at_pinned),
                PinnedCloseAction::kUnpinClose);
    }
  }
}

}  // namespace aerie_tab_model
