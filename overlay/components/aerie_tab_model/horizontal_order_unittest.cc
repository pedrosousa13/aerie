// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_tab_model/horizontal_order.h"

#include <map>
#include <string>
#include <vector>

#include "base/functional/bind.h"
#include "base/strings/string_number_conversions.h"
#include "base/test/simple_test_clock.h"
#include "components/aerie_tab_model/tab_model.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace aerie_tab_model {
namespace {

using Ids = std::vector<std::string>;

class HorizontalOrderTest : public testing::Test {
 protected:
  HorizontalOrderTest()
      : model_(&clock_, base::BindRepeating([](int* next) {
                 return "id-" + base::NumberToString((*next)++);
               },
                                             &next_id_)) {
    clock_.SetNow(base::Time::FromMillisecondsSinceUnixEpoch(1'700'000'000'000));
  }

  std::string Tab(Section section,
                  int position,
                  const std::string& parent = std::string()) {
    TabRecord tab;
    tab.section = section;
    tab.position = position;
    tab.parent = parent;
    return model_.AddTab(std::move(tab));
  }

  std::string Folder(Section section,
                     int position,
                     const std::string& parent = std::string(),
                     bool expanded = false) {
    FolderRecord folder;
    folder.section = section;
    folder.position = position;
    folder.parent = parent;
    folder.expanded = expanded;
    return model_.AddFolder(std::move(folder));
  }

  Ids Order() {
    return HorizontalStripOrder(model_, [](const std::string&) { return true; });
  }

  base::SimpleTestClock clock_;
  int next_id_ = 1;
  TabModel model_;
};

TEST_F(HorizontalOrderTest, FavoritesThenPinnedThenToday) {
  const std::string today = Tab(Section::kToday, 0);
  const std::string pinned = Tab(Section::kPinned, 0);
  const std::string favorite = Tab(Section::kFavorite, 0);
  EXPECT_EQ(Order(), (Ids{favorite, pinned, today}));
}

TEST_F(HorizontalOrderTest, PinnedFoldersAreFlattenedDepthFirstEvenWhenCollapsed) {
  const std::string a = Tab(Section::kPinned, 0);
  const std::string folder = Folder(Section::kPinned, 1);  // Collapsed.
  const std::string inner = Folder(Section::kPinned, 0, folder);
  const std::string deep = Tab(Section::kPinned, 0, inner);
  const std::string in_folder = Tab(Section::kPinned, 1, folder);
  const std::string z = Tab(Section::kPinned, 2);
  EXPECT_EQ(Order(), (Ids{a, deep, in_folder, z}));
}

TEST_F(HorizontalOrderTest, TodayFollowsTheModelPositions) {
  const std::string second = Tab(Section::kToday, 5);
  const std::string first = Tab(Section::kToday, 2);
  EXPECT_EQ(Order(), (Ids{first, second}));
}

TEST_F(HorizontalOrderTest, AnArchivedOrUnlistedTabIsNotInTheOrder) {
  const std::string shown = Tab(Section::kToday, 0);
  const std::string hidden = Tab(Section::kToday, 1);
  const Ids order = HorizontalStripOrder(
      model_, [&](const std::string& id) { return id != hidden; });
  EXPECT_EQ(order, (Ids{shown}));
}

TEST(StripMovesTest, NothingMovesWhenTheOrderIsRight) {
  EXPECT_TRUE(PlanStripMoves({"a", "b", "c"}, {"a", "b", "c"}).empty());
}

// Applies the moves to `order` the way TabStripModel::MoveWebContentsAt does.
Ids Apply(Ids order, const std::vector<StripMove>& moves) {
  for (const StripMove& move : moves) {
    std::string id = order[move.from];
    order.erase(order.begin() + move.from);
    order.insert(order.begin() + move.to, id);
  }
  return order;
}

TEST(StripMovesTest, TheMovesTurnTheStripIntoTheTarget) {
  const Ids current = {"t1", "p1", "t2", "f1", "p2"};
  const Ids target = {"f1", "p1", "p2", "t1", "t2"};
  EXPECT_EQ(Apply(current, PlanStripMoves(current, target)), target);
}

TEST(StripMovesTest, ATabThatIsNotInTheTargetKeepsItsPlaceAtTheEnd) {
  // "x" has no record (a tab that came in a moment ago).
  const Ids current = {"x", "b", "a"};
  const Ids target = {"a", "b"};
  EXPECT_EQ(Apply(current, PlanStripMoves(current, target)),
            (Ids{"a", "b", "x"}));
}

TEST(StripMovesTest, ATargetIdThatIsNotInTheStripIsSkipped) {
  // The tab lives in another window.
  const Ids current = {"b", "a"};
  const Ids target = {"a", "other-window", "b"};
  EXPECT_EQ(Apply(current, PlanStripMoves(current, target)), (Ids{"a", "b"}));
}

TEST_F(HorizontalOrderTest, AReorderInThePinnedGroupIsWrittenBackAsPositions) {
  const std::string p1 = Tab(Section::kPinned, 0);
  const std::string p2 = Tab(Section::kPinned, 1);
  const std::string p3 = Tab(Section::kPinned, 2);
  const std::string t1 = Tab(Section::kToday, 0);
  // The user drags p3 to the front of the pinned tabs in the strip.
  const std::vector<PositionChange> changes =
      PositionsFromStripOrder(model_, {p3, p1, p2, t1});
  std::map<std::string, int> by_id;
  for (const PositionChange& change : changes) {
    by_id[change.id] = change.position;
  }
  EXPECT_EQ(by_id[p3], 0);
  EXPECT_EQ(by_id[p1], 1);
  EXPECT_EQ(by_id[p2], 2);
  EXPECT_EQ(by_id.count(t1), 0u);  // Today follows the strip by itself.
}

TEST_F(HorizontalOrderTest, TheSlotsOfFoldersAreKeptWhenTabsSwap) {
  const std::string a = Tab(Section::kPinned, 0);
  const std::string folder = Folder(Section::kPinned, 1);
  const std::string b = Tab(Section::kPinned, 2);
  const std::vector<PositionChange> changes =
      PositionsFromStripOrder(model_, {b, a});
  std::map<std::string, int> by_id;
  for (const PositionChange& change : changes) {
    by_id[change.id] = change.position;
  }
  // a and b swap their own slots 0 and 2; the folder in slot 1 stays.
  EXPECT_EQ(by_id[b], 0);
  EXPECT_EQ(by_id[a], 2);
  EXPECT_EQ(by_id.count(folder), 0u);
}

TEST_F(HorizontalOrderTest, AStripInTheModelOrderChangesNothing) {
  const std::string p1 = Tab(Section::kPinned, 0);
  const std::string p2 = Tab(Section::kPinned, 1);
  EXPECT_TRUE(PositionsFromStripOrder(model_, {p1, p2}).empty());
}

TEST_F(HorizontalOrderTest, TabsOfDifferentFoldersAreNotMixed) {
  const std::string f1 = Folder(Section::kPinned, 0);
  const std::string f2 = Folder(Section::kPinned, 1);
  const std::string a = Tab(Section::kPinned, 0, f1);
  const std::string b = Tab(Section::kPinned, 0, f2);
  // The strip puts b before a: they are in different folders, so no sibling
  // changed order and nothing is written.
  EXPECT_TRUE(PositionsFromStripOrder(model_, {b, a}).empty());
}

}  // namespace
}  // namespace aerie_tab_model
