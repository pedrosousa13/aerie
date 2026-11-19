// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_tab_model/arc_import.h"

#include <string>
#include <utility>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/path_service.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/values.h"
#include "components/aerie_tab_model/arc_import_json.h"
#include "components/aerie_tab_model/tab_model_types.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace aerie_tab_model {

namespace {

// The hand-written files of testdata/arc (docs/arc-import.md lists them).
std::string ReadFixture(const std::string& name) {
  base::FilePath path;
  EXPECT_TRUE(base::PathService::Get(base::DIR_SRC_TEST_DATA_ROOT, &path));
  path = path.AppendASCII("components/aerie_tab_model/testdata/arc")
             .AppendASCII(name);
  std::string contents;
  EXPECT_TRUE(base::ReadFileToString(path, &contents)) << path;
  return contents;
}

ArcParseResult ParseFixture(const std::string& name) {
  std::optional<base::Value> value = ParseUntrustedJson(ReadFixture(name));
  EXPECT_TRUE(value.has_value()) << name;
  if (!value) {
    return ArcParseResult();
  }
  return ParseArcSidebar(*value);
}

int Count(const ArcSkipCounts& counts, ArcSkipReason reason) {
  auto it = counts.find(reason);
  return it == counts.end() ? 0 : it->second;
}

// "Name" for a tab, "[Name: a, b]" for a folder: the tree in one line.
std::string Describe(const std::vector<ArcPlanNode>& nodes) {
  std::string out;
  for (const ArcPlanNode& node : nodes) {
    if (!out.empty()) {
      out += ", ";
    }
    if (node.is_folder) {
      out += "[" + node.folder_name + ": " + Describe(node.children) + "]";
    } else {
      out += node.tab.last_title;
    }
  }
  return out;
}

std::string DescribeTabs(const std::vector<ArcPlanTab>& tabs) {
  std::string out;
  for (const ArcPlanTab& tab : tabs) {
    if (!out.empty()) {
      out += ", ";
    }
    out += tab.last_title;
  }
  return out;
}

// A one-Space file whose pinned container holds `item_entries` as given.
base::Value MakeFile(base::ListValue items, base::ListValue spaces) {
  base::DictValue container;
  container.Set("items", std::move(items));
  container.Set("spaces", std::move(spaces));
  base::ListValue containers;
  containers.Append(base::DictValue());
  containers.Append(std::move(container));
  base::DictValue sidebar;
  sidebar.Set("containers", std::move(containers));
  base::DictValue root;
  root.Set("sidebar", std::move(sidebar));
  return base::Value(std::move(root));
}

base::DictValue TabItem(const std::string& id,
                        const std::string& parent,
                        const std::string& url) {
  base::DictValue tab;
  tab.Set("savedURL", url);
  tab.Set("savedTitle", id);
  base::DictValue data;
  data.Set("tab", std::move(tab));
  base::DictValue item;
  item.Set("id", id);
  item.Set("parentID", parent);
  item.Set("data", std::move(data));
  return item;
}

base::DictValue FolderItem(const std::string& id,
                           const std::string& parent,
                           std::vector<std::string> children) {
  base::DictValue data;
  data.Set("list", base::DictValue());
  base::ListValue ids;
  for (const std::string& child : children) {
    ids.Append(child);
  }
  base::DictValue item;
  item.Set("id", id);
  item.Set("parentID", parent);
  item.Set("title", id);
  item.Set("childrenIds", std::move(ids));
  item.Set("data", std::move(data));
  return item;
}

base::ListValue OneSpaceList(const std::string& pinned_root) {
  base::ListValue container_ids;
  container_ids.Append("pinned");
  container_ids.Append(pinned_root);
  base::DictValue space;
  space.Set("id", "s1");
  space.Set("title", "S");
  space.Set("containerIDs", std::move(container_ids));
  base::ListValue spaces;
  spaces.Append("s1");
  spaces.Append(std::move(space));
  return spaces;
}

}  // namespace

TEST(ArcImportParserTest, OneSpaceGivesPinnedTreeAndFavorites) {
  ArcParseResult result = ParseFixture("one_space.json");
  ASSERT_FALSE(result.error.has_value());
  ASSERT_EQ(result.spaces.size(), 1u);
  const ArcSpacePlan& space = result.spaces[0];
  EXPECT_EQ(space.id, "s1");
  EXPECT_EQ(space.name, "Work Space");
  EXPECT_EQ(space.icon, "\xF0\x9F\xA7\xAA");  // U+1F9EA
  EXPECT_EQ(Describe(space.pinned), "Example, [Work: Two, Three], Four");
  EXPECT_EQ(DescribeTabs(space.favorites), "Calendar, Mail");
  EXPECT_EQ(space.pinned[0].tab.url, "https://example.com/");
  EXPECT_EQ(space.PinnedTabCount(), 4);
  EXPECT_EQ(space.FolderCount(), 1);
  EXPECT_TRUE(space.skipped.empty());
  EXPECT_TRUE(result.file_skipped.empty());
}

TEST(ArcImportParserTest, ThreeSpacesEachGetTheirOwnFavoritesAndLittleArcIsSkipped) {
  ArcParseResult result = ParseFixture("three_spaces.json");
  ASSERT_FALSE(result.error.has_value());
  ASSERT_EQ(result.spaces.size(), 3u);
  EXPECT_EQ(result.spaces[0].name, "Work");
  EXPECT_EQ(result.spaces[1].name, "Home");
  EXPECT_EQ(result.spaces[2].name, "");  // Untitled: the page names it.
  EXPECT_EQ(result.spaces[0].icon, "\xF0\x9F\x8F\x9B");
  EXPECT_EQ(result.spaces[1].icon, "");
  EXPECT_EQ(Describe(result.spaces[0].pinned), "Work one, Work two");
  EXPECT_EQ(Describe(result.spaces[1].pinned), "Home");
  EXPECT_EQ(Describe(result.spaces[2].pinned), "");
  // The Favorites of the Space's own Arc profile.
  EXPECT_EQ(DescribeTabs(result.spaces[0].favorites), "Default fav");
  EXPECT_EQ(DescribeTabs(result.spaces[1].favorites), "Profile 1 fav");
  EXPECT_EQ(DescribeTabs(result.spaces[2].favorites), "Default fav");
  EXPECT_EQ(Count(result.file_skipped, ArcSkipReason::kLittleArc), 1);
}

TEST(ArcImportParserTest, FoldersNestFiveLevelsAndDeeperOnesAreMerged) {
  ArcParseResult result = ParseFixture("nested_folders.json");
  ASSERT_EQ(result.spaces.size(), 1u);
  const ArcSpacePlan& space = result.spaces[0];
  // L6 and L7 are merged into L5; their tabs stay, in order.
  EXPECT_EQ(Describe(space.pinned),
            "[L1: [L2: [L3: [L4: [L5: in L5, in L6, in L7]]]], in L1]");
  EXPECT_EQ(space.FolderCount(), 5);
  EXPECT_EQ(space.PinnedTabCount(), 4);
  EXPECT_EQ(Count(space.skipped, ArcSkipReason::kFolderMerged), 2);
}

TEST(ArcImportParserTest, FavoritesStopAtTheCap) {
  ArcParseResult result = ParseFixture("favorites.json");
  ASSERT_EQ(result.spaces.size(), 1u);
  const ArcSpacePlan& space = result.spaces[0];
  ASSERT_EQ(space.favorites.size(), 12u);
  EXPECT_EQ(space.favorites.front().last_title, "Fav 1");
  EXPECT_EQ(space.favorites.back().last_title, "Fav 12");
  EXPECT_EQ(Count(space.skipped, ArcSkipReason::kFavoritesFull), 2);
}

TEST(ArcImportParserTest, CustomTitlesBecomeCustomNames) {
  ArcParseResult result = ParseFixture("custom_titles.json");
  ASSERT_EQ(result.spaces.size(), 1u);
  const std::vector<ArcPlanNode>& pinned = result.spaces[0].pinned;
  ASSERT_EQ(pinned.size(), 4u);
  EXPECT_EQ(pinned[0].tab.custom_name, "My Mail");
  EXPECT_EQ(pinned[0].tab.last_title, "Inbox (3) - Mail");
  // No name, the same name as the page, a blank name: no custom name.
  EXPECT_FALSE(pinned[1].tab.custom_name.has_value());
  EXPECT_FALSE(pinned[2].tab.custom_name.has_value());
  EXPECT_FALSE(pinned[3].tab.custom_name.has_value());
  EXPECT_EQ(result.spaces[0].CustomNameCount(), 1);
}

TEST(ArcImportParserTest, OnlyHttpAndHttpsAddressesAreImported) {
  ArcParseResult result = ParseFixture("bad_schemes.json");
  ASSERT_EQ(result.spaces.size(), 1u);
  const ArcSpacePlan& space = result.spaces[0];
  // javascript:, file:, data:, chrome:, aerie:, arc: and a padded, upper-case
  // JavaScript: in Pinned, and javascript: in Favorites.
  EXPECT_EQ(Count(space.skipped, ArcSkipReason::kUnsupportedScheme), 8);
  EXPECT_EQ(Count(space.skipped, ArcSkipReason::kNoUsableAddress), 0);
  EXPECT_EQ(DescribeTabs(space.favorites), "Fav ok");
  ASSERT_EQ(space.pinned.size(), 3u);
  // Canonical, and the credentials are gone.
  EXPECT_EQ(space.pinned[0].tab.url, "https://upper.example/Path");
  EXPECT_EQ(space.pinned[1].tab.url, "http://plain.example/");
  EXPECT_EQ(space.pinned[2].tab.url, "https://creds.example/");
}

TEST(ArcImportParserTest, TitlesWithMarkupStayText) {
  ArcParseResult result = ParseFixture("markup_titles.json");
  ASSERT_EQ(result.spaces.size(), 1u);
  const ArcSpacePlan& space = result.spaces[0];
  // The markup is kept as text (every surface shows it as text); the control
  // characters are not.
  EXPECT_EQ(space.name, "<svg onload=alert(3)>");
  ASSERT_EQ(space.pinned.size(), 2u);
  EXPECT_TRUE(space.pinned[0].is_folder);
  EXPECT_EQ(space.pinned[0].folder_name, "<img src=x onerror=alert(1)>");
  EXPECT_EQ(space.pinned[1].tab.custom_name, "<script>alert('name')</script>");
  EXPECT_EQ(space.pinned[1].tab.last_title,
            "<b onclick=alert(2)>bold</b>&amp; end");
}

TEST(ArcImportParserTest, AFileThatIsNotAnArcSidebarFails) {
  for (const char* name : {"not_an_object.json", "no_containers.json"}) {
    ArcParseResult result = ParseFixture(name);
    ASSERT_TRUE(result.error.has_value()) << name;
    EXPECT_EQ(*result.error, ArcParseError::kNotAnArcSidebar) << name;
    EXPECT_TRUE(result.spaces.empty()) << name;
  }
}

TEST(ArcImportParserTest, AFileWithoutSpacesFails) {
  ArcParseResult result = ParseFixture("missing_keys.json");
  ASSERT_TRUE(result.error.has_value());
  EXPECT_EQ(*result.error, ArcParseError::kNoSpaces);
}

TEST(ArcImportParserTest, WrongTypesAreSkippedNotTrusted) {
  ArcParseResult result = ParseFixture("wrong_types.json");
  ASSERT_FALSE(result.error.has_value());
  // "s1" is read (its title is not text, so it has none); `"s2", 5` is not a
  // Space.
  ASSERT_EQ(result.spaces.size(), 1u);
  EXPECT_EQ(result.spaces[0].name, "");
  // containerIDs is an object, not a list: no pinned container.
  EXPECT_TRUE(result.spaces[0].pinned.empty());
  EXPECT_TRUE(result.spaces[0].favorites.empty());
}

TEST(ArcImportParserTest, TextThatIsNotJsonIsRefused) {
  EXPECT_FALSE(ParseUntrustedJson(ReadFixture("bad_json.json")).has_value());
  EXPECT_FALSE(ParseUntrustedJson("").has_value());
}

TEST(ArcImportParserTest, JsonNestedTooDeepIsRefused) {
  EXPECT_FALSE(
      ParseUntrustedJson(std::string(100'000, '[') + std::string(100'000, ']'))
          .has_value());
}

TEST(ArcImportParserTest, ItemsPastTheCapAreNotReadAndAreCounted) {
  // One container and 10 009 tabs: 10 010 entries, 10 past the cap.
  base::ListValue items;
  base::ListValue children;
  base::DictValue root_data;
  root_data.Set("itemContainer", base::DictValue());
  base::DictValue root_item;
  root_item.Set("id", "p");
  root_item.Set("data", std::move(root_data));
  constexpr int kTabs = 10'009;
  for (int i = 0; i < kTabs; ++i) {
    children.Append("t" + base::NumberToString(i));
  }
  root_item.Set("childrenIds", std::move(children));
  items.Append("p");
  items.Append(std::move(root_item));
  for (int i = 0; i < kTabs; ++i) {
    const std::string id = "t" + base::NumberToString(i);
    items.Append(id);
    items.Append(TabItem(id, "p", "https://x.example/" + id));
  }
  // The list holds an id and an item for each entry; the cap counts entries.
  ArcParseResult result =
      ParseArcSidebar(MakeFile(std::move(items), OneSpaceList("p")));
  ASSERT_EQ(result.spaces.size(), 1u);
  EXPECT_EQ(result.spaces[0].PinnedTabCount(), 9'999);
  EXPECT_EQ(Count(result.file_skipped, ArcSkipReason::kTooMany), 10);
}

TEST(ArcImportParserTest, ASpaceListPastTheCapIsCut) {
  base::ListValue spaces;
  for (int i = 0; i < 101; ++i) {
    base::DictValue space;
    space.Set("id", "s" + base::NumberToString(i));
    space.Set("title", "S" + base::NumberToString(i));
    spaces.Append(std::move(space));
  }
  ArcParseResult result =
      ParseArcSidebar(MakeFile(base::ListValue(), std::move(spaces)));
  ASSERT_FALSE(result.error.has_value());
  EXPECT_EQ(result.spaces.size(), 100u);
  EXPECT_EQ(result.spaces.back().name, "S99");
  EXPECT_EQ(Count(result.file_skipped, ArcSkipReason::kTooMany), 1);
}

TEST(ArcImportParserTest, ATreeDeeperThanTheCapIsCutAndTerminates) {
  // p > f1 > f2 > ... > f40, each folder also holds one tab.
  base::ListValue items;
  base::DictValue root_data;
  root_data.Set("itemContainer", base::DictValue());
  base::ListValue root_children;
  root_children.Append("f1");
  base::DictValue root_item;
  root_item.Set("id", "p");
  root_item.Set("childrenIds", std::move(root_children));
  root_item.Set("data", std::move(root_data));
  items.Append("p");
  items.Append(std::move(root_item));
  for (int i = 1; i <= 40; ++i) {
    const std::string f = "f" + base::NumberToString(i);
    const std::string t = "t" + base::NumberToString(i);
    const std::string parent =
        i == 1 ? "p" : "f" + base::NumberToString(i - 1);
    std::vector<std::string> kids = {t};
    if (i < 40) {
      kids.push_back("f" + base::NumberToString(i + 1));
    }
    items.Append(f);
    items.Append(FolderItem(f, parent, kids));
    items.Append(t);
    items.Append(TabItem(t, f, "https://deep.example/" + t));
  }
  ArcParseResult result =
      ParseArcSidebar(MakeFile(std::move(items), OneSpaceList("p")));
  ASSERT_EQ(result.spaces.size(), 1u);
  const ArcSpacePlan& space = result.spaces[0];
  // f_k is at depth k and t_k at depth k + 1. Depth 32 is the last allowed, so
  // t1 ... t31 come in, and at f32 the tab t32 and the folder f33 are cut.
  EXPECT_EQ(space.PinnedTabCount(), 31);
  EXPECT_EQ(Count(space.skipped, ArcSkipReason::kTooDeep), 2);
}

TEST(ArcImportParserTest, ALoopInChildrenEnds) {
  base::ListValue items;
  base::DictValue root_data;
  root_data.Set("itemContainer", base::DictValue());
  base::ListValue root_children;
  root_children.Append("f1");
  base::DictValue root_item;
  root_item.Set("id", "p");
  root_item.Set("childrenIds", std::move(root_children));
  root_item.Set("data", std::move(root_data));
  items.Append("p");
  items.Append(std::move(root_item));
  items.Append("f1");
  items.Append(FolderItem("f1", "p", {"f1", "t1", "p"}));
  items.Append("t1");
  items.Append(TabItem("t1", "f1", "https://loop.example/"));
  ArcParseResult result =
      ParseArcSidebar(MakeFile(std::move(items), OneSpaceList("p")));
  ASSERT_EQ(result.spaces.size(), 1u);
  EXPECT_EQ(result.spaces[0].PinnedTabCount(), 1);
}

TEST(ArcImportParserTest, ItemKindsAerieDoesNotKnowAreCountedAsArcOnly) {
  base::ListValue items;
  base::DictValue root_data;
  root_data.Set("itemContainer", base::DictValue());
  base::ListValue root_children;
  root_children.Append("e1");
  base::DictValue root_item;
  root_item.Set("id", "p");
  root_item.Set("childrenIds", std::move(root_children));
  root_item.Set("data", std::move(root_data));
  items.Append("p");
  items.Append(std::move(root_item));
  base::DictValue easel_data;
  easel_data.Set("easel", base::DictValue());
  base::DictValue easel;
  easel.Set("id", "e1");
  easel.Set("data", std::move(easel_data));
  items.Append("e1");
  items.Append(std::move(easel));
  ArcParseResult result =
      ParseArcSidebar(MakeFile(std::move(items), OneSpaceList("p")));
  ASSERT_EQ(result.spaces.size(), 1u);
  EXPECT_EQ(Count(result.spaces[0].skipped, ArcSkipReason::kArcOnlyItem), 1);
}

TEST(ArcImportParserTest, ATabWithoutAnAddressIsSkipped) {
  EXPECT_EQ(ClassifyArcUrl("").reason, ArcSkipReason::kNoUsableAddress);
  EXPECT_EQ(ClassifyArcUrl("not a url").reason,
            ArcSkipReason::kNoUsableAddress);
  EXPECT_EQ(ClassifyArcUrl("https://a.example/" + std::string(9000, 'x'))
                .reason,
            ArcSkipReason::kNoUsableAddress);
  EXPECT_EQ(ClassifyArcUrl("ftp://a.example/").reason,
            ArcSkipReason::kUnsupportedScheme);
  EXPECT_EQ(ClassifyArcUrl("blob:https://a.example/x").reason,
            ArcSkipReason::kUnsupportedScheme);
  EXPECT_EQ(ClassifyArcUrl(" https://a.example/ ").url, "https://a.example/");
}

TEST(ArcImportParserTest, TitlesAreCleanedAndCut) {
  EXPECT_EQ(SanitizeArcTitle("  a\tb\nc\x01\x7f" "d\xE2\x80\xAE" "e  "),
            "a b cde");
  EXPECT_EQ(SanitizeArcTitle("<b>x</b>"), "<b>x</b>");
  const std::string long_title(5000, 'a');
  EXPECT_EQ(SanitizeArcTitle(long_title).size(), kArcMaxTitleLength);
  // 1023 letters and then U+1F600, which is two UTF-16 units: the cut must not
  // keep half of it.
  const std::string edge = std::string(1023, 'a') + "\xF0\x9F\x98\x80";
  EXPECT_EQ(SanitizeArcTitle(edge), std::string(1023, 'a'));
  // Two units that fit are kept.
  const std::string fits = std::string(1022, 'a') + "\xF0\x9F\x98\x80";
  EXPECT_EQ(SanitizeArcTitle(fits), fits);
}

TEST(ArcImportParserTest, TheLimitsAreTheOnesInTheIssue) {
  EXPECT_EQ(kArcMaxFileBytes, 32u * 1024 * 1024);
  EXPECT_EQ(kArcMaxSpaces, 100u);
  EXPECT_EQ(kArcMaxItems, 10'000u);
  EXPECT_EQ(kArcMaxDepth, 32);
  EXPECT_EQ(kArcMaxTitleLength, 1024u);
}

}  // namespace aerie_tab_model
