// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_sidebar/aerie_sidebar_move.h"

#include "base/functional/bind.h"
#include "base/strings/string_number_conversions.h"
#include "base/test/simple_test_clock.h"
#include "components/aerie_tab_model/tab_model.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace aerie_sidebar {

namespace {

using aerie_tab_model::FolderRecord;
using aerie_tab_model::ItemKind;
using aerie_tab_model::Section;
using aerie_tab_model::TabRecord;
using aerie_tab_model::TreeRow;

class MoveTest : public testing::Test {
 protected:
  MoveTest()
      : model_(&clock_,
               base::BindRepeating(
                   [](int* next) { return "id-" + base::NumberToString((*next)++); },
                   &next_id_)) {
    clock_.SetNow(base::Time::FromMillisecondsSinceUnixEpoch(1'700'000'000'000));
  }

  std::string Folder(const std::string& parent, int position,
                     Section section = Section::kToday) {
    FolderRecord folder;
    folder.parent = parent;
    folder.position = position;
    folder.section = section;
    folder.expanded = true;
    return model_.AddFolder(std::move(folder));
  }
  std::string Tab(const std::string& parent, int position,
                  Section section = Section::kToday) {
    TabRecord tab;
    tab.parent = parent;
    tab.position = position;
    tab.section = section;
    return model_.AddTab(std::move(tab));
  }

  MoveResult Move(ItemKind kind, const std::string& id, Section section,
                  const std::string& container, int index) {
    MoveRequest request;
    request.kind = kind;
    request.id = id;
    request.section = section;
    request.container = container;
    request.index = index;
    return ApplyMove(
        model_, request, [](const std::string&) { return true; },
        [](const std::string& tab) { return "https://" + tab + ".test/"; });
  }

  std::vector<std::string> Order(Section section,
                                 const std::string& parent = std::string()) {
    std::vector<std::string> ids;
    for (const auto& c : aerie_tab_model::OrderedChildren(
             model_, section, parent, false,
             [](const std::string&) { return true; })) {
      ids.push_back(c.id);
    }
    return ids;
  }

  std::vector<TreeRow> Rows(Section section) {
    return aerie_tab_model::BuildVisibleRows(
        model_, section, [](const std::string&) { return true; });
  }

  base::SimpleTestClock clock_;
  int next_id_ = 1;
  aerie_tab_model::TabModel model_;
};

}  // namespace

TEST_F(MoveTest, AMoveInsideASectionRenumbersTheSiblings) {
  const std::string a = Tab("", 5), b = Tab("", 9), c = Tab("", 20);
  EXPECT_EQ(Move(ItemKind::kTab, c, Section::kToday, "", 0), MoveResult::kMoved);
  EXPECT_EQ(Order(Section::kToday), (std::vector<std::string>{c, a, b}));
  EXPECT_EQ(model_.GetTab(c)->position, 0);
  EXPECT_EQ(model_.GetTab(b)->position, 2);
  // Index past the end is the end.
  EXPECT_EQ(Move(ItemKind::kTab, c, Section::kToday, "", 9), MoveResult::kMoved);
  EXPECT_EQ(Order(Section::kToday), (std::vector<std::string>{a, b, c}));
}

TEST_F(MoveTest, ADropOnTheSamePlaceWritesNothing) {
  const std::string a = Tab("", 0), b = Tab("", 1);
  const int64_t before = model_.next_change();
  EXPECT_EQ(Move(ItemKind::kTab, a, Section::kToday, "", 0),
            MoveResult::kUnchanged);
  EXPECT_EQ(Move(ItemKind::kTab, b, Section::kToday, "", 1),
            MoveResult::kUnchanged);
  EXPECT_EQ(model_.next_change(), before);
}

TEST_F(MoveTest, ATabGoesIntoAFolderAtTheIndex) {
  const std::string folder = Folder("", 0);
  const std::string x = Tab(folder, 0), y = Tab(folder, 1);
  const std::string t = Tab("", 1);
  EXPECT_EQ(Move(ItemKind::kTab, t, Section::kToday, folder, 1),
            MoveResult::kMoved);
  EXPECT_EQ(Order(Section::kToday, folder), (std::vector<std::string>{x, t, y}));
  EXPECT_EQ(model_.GetTab(t)->parent, folder);
}

TEST_F(MoveTest, PinningSetsAUrlAndUnpinningClearsIt) {
  const std::string t = Tab("", 0);
  EXPECT_EQ(Move(ItemKind::kTab, t, Section::kPinned, "", 0), MoveResult::kMoved);
  EXPECT_EQ(model_.GetTab(t)->section, Section::kPinned);
  EXPECT_EQ(model_.GetTab(t)->pinned_url, "https://" + t + ".test/");
  EXPECT_EQ(Move(ItemKind::kTab, t, Section::kToday, "", 0), MoveResult::kMoved);
  EXPECT_FALSE(model_.GetTab(t)->pinned_url.has_value());
}

TEST_F(MoveTest, FavoritesRememberTheSectionAndStopAtTwelve) {
  const std::string t = Tab("", 0, Section::kPinned);
  EXPECT_EQ(Move(ItemKind::kTab, t, Section::kFavorite, "", 0),
            MoveResult::kMoved);
  EXPECT_EQ(model_.GetTab(t)->previous_section, Section::kPinned);
  for (int i = 0; i < 11; ++i) {
    Tab("", i + 1, Section::kFavorite);
  }
  const std::string extra = Tab("", 0);
  EXPECT_EQ(Move(ItemKind::kTab, extra, Section::kFavorite, "", 0),
            MoveResult::kRefusedFavoritesFull);
  EXPECT_EQ(model_.GetTab(extra)->section, Section::kToday);
  // Reordering a Favorite is not a 13th.
  EXPECT_EQ(Move(ItemKind::kTab, t, Section::kFavorite, "", 5),
            MoveResult::kMoved);
  // A folder cannot go to Favorites.
  const std::string folder = Folder("", 3);
  EXPECT_EQ(Move(ItemKind::kFolder, folder, Section::kFavorite, "", 0),
            MoveResult::kInvalid);
}

TEST_F(MoveTest, AFolderTakesItsTabsAndFoldersToAnotherSection) {
  const std::string outer = Folder("", 0);
  const std::string inner = Folder(outer, 0);
  const std::string a = Tab(outer, 1), b = Tab(inner, 0);
  EXPECT_EQ(Move(ItemKind::kFolder, outer, Section::kPinned, "", 0),
            MoveResult::kMoved);
  EXPECT_EQ(model_.GetFolder(inner)->section, Section::kPinned);
  EXPECT_EQ(model_.GetTab(a)->section, Section::kPinned);
  EXPECT_EQ(model_.GetTab(b)->section, Section::kPinned);
  EXPECT_EQ(model_.GetTab(b)->parent, inner);
  EXPECT_TRUE(model_.GetTab(b)->pinned_url.has_value());
  EXPECT_EQ(Move(ItemKind::kFolder, outer, Section::kToday, "", 0),
            MoveResult::kMoved);
  EXPECT_FALSE(model_.GetTab(b)->pinned_url.has_value());
  EXPECT_EQ(model_.GetTab(b)->section, Section::kToday);
}

TEST_F(MoveTest, TheDepthLimitAndLoopsAreRefused) {
  std::string parent, level5;
  for (int i = 0; i < 5; ++i) {
    parent = Folder(parent, 0);
    level5 = parent;
  }
  const std::string loose = Folder("", 1);
  EXPECT_EQ(Move(ItemKind::kFolder, loose, Section::kToday, level5, 0),
            MoveResult::kRefusedTooDeep);
  const std::string tab = Tab("", 2);
  EXPECT_EQ(Move(ItemKind::kTab, tab, Section::kToday, level5, 0),
            MoveResult::kMoved);
  const std::string top = aerie_tab_model::OrderedChildren(
      model_, Section::kToday, "", false,
      [](const std::string&) { return true; })[0].id;
  EXPECT_EQ(Move(ItemKind::kFolder, top, Section::kToday, level5, 0),
            MoveResult::kInvalid);
}

TEST_F(MoveTest, UnlistedSiblingsKeepTheirOrderAndAreNotCounted) {
  const std::string a = Tab("", 0), hidden = Tab("", 1), b = Tab("", 2);
  const std::string t = Tab("", 3);
  MoveRequest request;
  request.id = t;
  request.index = 1;  // between the listed a and b
  ApplyMove(
      model_, request, [&](const std::string& id) { return id != hidden; },
      [](const std::string&) { return std::string(); });
  EXPECT_EQ(Order(Section::kToday),
            (std::vector<std::string>{a, hidden, t, b}));
}

TEST_F(MoveTest, KeyboardUpDownMoveAmongSiblings) {
  const std::string a = Tab("", 0), b = Tab("", 1), c = Tab("", 2);
  auto rows = Rows(Section::kToday);
  KeyboardMove down = ComputeKeyboardMove(model_, Section::kToday, rows, 0,
                                          MoveKey::kDown);
  ASSERT_TRUE(down.applies);
  EXPECT_EQ(down.request.index, 1);
  EXPECT_EQ(down.announce_position, 2);
  EXPECT_EQ(down.announce_count, 3);
  EXPECT_FALSE(ComputeKeyboardMove(model_, Section::kToday, rows, 0,
                                   MoveKey::kUp).applies);
  EXPECT_FALSE(ComputeKeyboardMove(model_, Section::kToday, rows, 2,
                                   MoveKey::kDown).applies);
  (void)a; (void)b; (void)c;
}

TEST_F(MoveTest, KeyboardIndentGoesIntoThePreviousFolderAndOutdentComesOut) {
  const std::string folder = Folder("", 0);
  Tab(folder, 0);
  const std::string t = Tab("", 1);
  auto rows = Rows(Section::kToday);  // folder, inner, t
  KeyboardMove in = ComputeKeyboardMove(model_, Section::kToday, rows, 2,
                                        MoveKey::kIndent);
  ASSERT_TRUE(in.applies);
  EXPECT_EQ(in.request.container, folder);
  EXPECT_EQ(in.announce_container, folder);
  EXPECT_EQ(in.announce_position, 2);
  EXPECT_EQ(in.announce_count, 2);
  ASSERT_EQ(Move(ItemKind::kTab, t, Section::kToday, in.request.container,
                 in.request.index), MoveResult::kMoved);
  rows = Rows(Section::kToday);  // folder, inner, t (now inside)
  KeyboardMove out = ComputeKeyboardMove(model_, Section::kToday, rows, 2,
                                         MoveKey::kOutdent);
  ASSERT_TRUE(out.applies);
  EXPECT_EQ(out.request.container, "");
  EXPECT_EQ(out.request.index, 1);
  EXPECT_EQ(out.announce_position, 2);
  EXPECT_EQ(out.announce_count, 2);
  // At the top level there is nothing to leave, and a tab has no folder before.
  EXPECT_FALSE(ComputeKeyboardMove(model_, Section::kToday, rows, 0,
                                   MoveKey::kOutdent).applies);
}

TEST_F(MoveTest, KeyboardIndentRespectsTheDepthLimit) {
  std::string parent, level5;
  for (int i = 0; i < 4; ++i) {
    parent = Folder(parent, 0);
  }
  level5 = Folder(parent, 0);  // level 5, a sibling chain: level4 holds it
  const std::string mover = Folder(parent, 1);  // a folder next to level5
  auto rows = Rows(Section::kToday);
  int index = -1;
  for (size_t i = 0; i < rows.size(); ++i) {
    if (rows[i].id == mover) {
      index = static_cast<int>(i);
    }
  }
  KeyboardMove in = ComputeKeyboardMove(model_, Section::kToday, rows, index,
                                        MoveKey::kIndent);
  EXPECT_FALSE(in.applies);
  EXPECT_TRUE(in.too_deep);
  (void)level5;
}

TEST_F(MoveTest, KeyboardIndentNeedsAFolderBeforeIt) {
  Tab("", 0);
  Tab("", 1);
  EXPECT_FALSE(ComputeKeyboardMove(model_, Section::kToday,
                                   Rows(Section::kToday), 1, MoveKey::kIndent)
                   .applies);
}

}  // namespace aerie_sidebar
