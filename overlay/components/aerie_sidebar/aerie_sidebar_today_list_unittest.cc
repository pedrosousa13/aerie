// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_sidebar/aerie_sidebar_today_list.h"

#include <string>
#include <vector>

#include "base/functional/bind.h"
#include "base/strings/string_number_conversions.h"
#include "base/test/simple_test_clock.h"
#include "base/time/time.h"
#include "components/aerie_tab_model/tab_model.h"
#include "components/aerie_tab_model/tab_model_types.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace aerie_sidebar {
namespace {

using aerie_tab_model::Section;
using aerie_tab_model::TabRecord;

class RecordingDelegate : public TodayList::Delegate {
 public:
  void OnRowInserted(int index, const std::string& id) override {
    events.push_back("insert " + base::NumberToString(index) + " " + id);
  }
  void OnRowRemoved(int index, const std::string& id) override {
    events.push_back("remove " + base::NumberToString(index) + " " + id);
  }
  void OnRowsReset() override { events.push_back("reset"); }
  std::vector<std::string> events;
};

class TodayListTest : public testing::Test {
 public:
  TodayListTest()
      : model_(&clock_, base::BindRepeating([]() {
          static int next = 0;
          return "t" + base::NumberToString(++next);
        })) {
    clock_.SetNow(base::Time::FromSecondsSinceUnixEpoch(1700000000));
  }

  std::string Add(int position, Section section = Section::kToday) {
    TabRecord tab;
    tab.section = section;
    tab.position = position;
    return model_.AddTab(tab);
  }

 protected:
  base::SimpleTestClock clock_;
  aerie_tab_model::TabModel model_;
  RecordingDelegate delegate_;
};

TEST_F(TodayListTest, ListsTodayTabsInPositionOrder) {
  const std::string b = Add(1);
  const std::string a = Add(0);
  Add(0, Section::kPinned);
  TodayList list(&model_, &delegate_);
  EXPECT_EQ(list.rows(), (std::vector<std::string>{a, b}));
}

TEST_F(TodayListTest, AnAddedTabIsOneInsertAtItsPosition) {
  const std::string a = Add(0);
  const std::string c = Add(2);
  TodayList list(&model_, &delegate_);
  const std::string b = Add(1);
  EXPECT_EQ(delegate_.events, (std::vector<std::string>{"insert 1 " + b}));
  EXPECT_EQ(list.rows(), (std::vector<std::string>{a, b, c}));
}

TEST_F(TodayListTest, ARemovedTabIsOneRemoveAndTheListMatchesTheModel) {
  const std::string a = Add(0);
  const std::string b = Add(1);
  TodayList list(&model_, &delegate_);
  ASSERT_TRUE(model_.RemoveTab(a));
  EXPECT_EQ(delegate_.events, (std::vector<std::string>{"remove 0 " + a}));
  EXPECT_EQ(list.rows(), (std::vector<std::string>{b}));
}

TEST_F(TodayListTest, AnArchivedOrPinnedTabLeavesTheList) {
  const std::string a = Add(0);
  const std::string b = Add(1);
  TodayList list(&model_, &delegate_);
  ASSERT_TRUE(model_.UpdateTab(a, [&](TabRecord& t) {
    t.archived_at = clock_.Now();
  }));
  ASSERT_TRUE(model_.MoveTab(b, Section::kPinned, "", 0));
  EXPECT_TRUE(list.rows().empty());
  EXPECT_EQ(delegate_.events.size(), 2u);
}

TEST_F(TodayListTest, ChangesThatLeaveTheRowsAloneSayNothing) {
  const std::string a = Add(0);
  TodayList list(&model_, &delegate_);
  ASSERT_TRUE(model_.UpdateTab(a, [](TabRecord& t) { t.sleeping = true; }));
  EXPECT_TRUE(delegate_.events.empty());
}

TEST_F(TodayListTest, AReorderIsAReset) {
  const std::string a = Add(0);
  const std::string b = Add(1);
  TodayList list(&model_, &delegate_);
  ASSERT_TRUE(model_.MoveTab(a, Section::kToday, "", 5));
  EXPECT_EQ(delegate_.events, (std::vector<std::string>{"reset"}));
  EXPECT_EQ(list.rows(), (std::vector<std::string>{b, a}));
}

TEST(HandleListKeyTest, ArrowsStopAtTheEndsAndHomeAndEndJump) {
  EXPECT_EQ(HandleListKey(3, 1, ListKey::kDown).focus_index, 2);
  EXPECT_EQ(HandleListKey(3, 2, ListKey::kDown).focus_index, 2);
  EXPECT_EQ(HandleListKey(3, 1, ListKey::kUp).focus_index, 0);
  EXPECT_EQ(HandleListKey(3, 0, ListKey::kUp).focus_index, 0);
  EXPECT_EQ(HandleListKey(3, 1, ListKey::kHome).focus_index, 0);
  EXPECT_EQ(HandleListKey(3, 0, ListKey::kEnd).focus_index, 2);
  EXPECT_FALSE(HandleListKey(3, 1, ListKey::kDown).close_focused);
}

TEST(HandleListKeyTest, DeleteClosesAndTheStopLandsOnTheNextRowOrTheNewLast) {
  ListKeyResult middle = HandleListKey(3, 1, ListKey::kDelete);
  EXPECT_TRUE(middle.close_focused);
  EXPECT_EQ(middle.focus_index, 1);
  EXPECT_EQ(HandleListKey(3, 2, ListKey::kDelete).focus_index, 1);
  EXPECT_EQ(HandleListKey(1, 0, ListKey::kDelete).focus_index, -1);
}

TEST(HandleListKeyTest, AnEmptyListHasNoStopAndAStrayIndexCountsAsTheFirstRow) {
  EXPECT_EQ(HandleListKey(0, 0, ListKey::kDown).focus_index, -1);
  EXPECT_FALSE(HandleListKey(0, 0, ListKey::kDelete).close_focused);
  EXPECT_EQ(HandleListKey(3, 9, ListKey::kDown).focus_index, 1);
}

TEST(HandleGridKeyTest, ArrowsMoveInTwoDimensionsAndDoNotWrap) {
  // Ten tiles in four columns: rows are 0-3, 4-7, 8-9.
  EXPECT_EQ(HandleGridKey(10, 4, 5, GridKey::kRight).focus_index, 6);
  EXPECT_EQ(HandleGridKey(10, 4, 7, GridKey::kRight).focus_index, 7);
  EXPECT_EQ(HandleGridKey(10, 4, 9, GridKey::kRight).focus_index, 9);
  EXPECT_EQ(HandleGridKey(10, 4, 5, GridKey::kLeft).focus_index, 4);
  EXPECT_EQ(HandleGridKey(10, 4, 4, GridKey::kLeft).focus_index, 4);
  EXPECT_EQ(HandleGridKey(10, 4, 5, GridKey::kUp).focus_index, 1);
  EXPECT_EQ(HandleGridKey(10, 4, 1, GridKey::kUp).focus_index, 1);
  EXPECT_EQ(HandleGridKey(10, 4, 5, GridKey::kDown).focus_index, 9);
  EXPECT_EQ(HandleGridKey(10, 4, 2, GridKey::kDown).focus_index, 6);
}

TEST(HandleGridKeyTest, DownFromAColumnWithNoTileBelowLandsOnTheLastTile) {
  // Tile 3 has no tile below it (row 3 has two tiles); Down goes to the last.
  EXPECT_EQ(HandleGridKey(10, 4, 7, GridKey::kDown).focus_index, 9);
  EXPECT_EQ(HandleGridKey(10, 4, 9, GridKey::kDown).focus_index, 9);
  EXPECT_EQ(HandleGridKey(4, 4, 2, GridKey::kDown).focus_index, 2);
}

TEST(HandleGridKeyTest, HomeAndEndGoToTheRowAndControlGoesToTheGrid) {
  EXPECT_EQ(HandleGridKey(10, 4, 6, GridKey::kRowStart).focus_index, 4);
  EXPECT_EQ(HandleGridKey(10, 4, 5, GridKey::kRowEnd).focus_index, 7);
  EXPECT_EQ(HandleGridKey(10, 4, 8, GridKey::kRowEnd).focus_index, 9);
  EXPECT_EQ(HandleGridKey(10, 4, 6, GridKey::kGridStart).focus_index, 0);
  EXPECT_EQ(HandleGridKey(10, 4, 6, GridKey::kGridEnd).focus_index, 9);
}

TEST(HandleGridKeyTest, DeleteClosesAndAnEmptyGridHasNoStop) {
  const ListKeyResult middle = HandleGridKey(5, 4, 2, GridKey::kDelete);
  EXPECT_TRUE(middle.close_focused);
  EXPECT_EQ(middle.focus_index, 2);
  EXPECT_EQ(HandleGridKey(5, 4, 4, GridKey::kDelete).focus_index, 3);
  EXPECT_EQ(HandleGridKey(0, 4, 0, GridKey::kRight).focus_index, -1);
}

TEST_F(TodayListTest, APinnedListShowsOnlyThePinnedSection) {
  const std::string pinned = Add(0, Section::kPinned);
  Add(0);
  Add(0, Section::kFavorite);
  TodayList list(&model_, &delegate_, Section::kPinned);
  EXPECT_EQ(list.rows(), (std::vector<std::string>{pinned}));
}

TEST_F(TodayListTest, AFavoritesListFollowsATabThatMovesInAndOut) {
  const std::string today = Add(0);
  TodayList favorites(&model_, &delegate_, Section::kFavorite);
  EXPECT_TRUE(favorites.rows().empty());
  ASSERT_TRUE(model_.MoveTab(today, Section::kFavorite, std::string(), 0));
  EXPECT_EQ(favorites.rows(), (std::vector<std::string>{today}));
  ASSERT_TRUE(model_.MoveTab(today, Section::kToday, std::string(), 0));
  EXPECT_TRUE(favorites.rows().empty());
}

}  // namespace
}  // namespace aerie_sidebar
