// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_adblock/browser/filter_list_storage.h"

#include <string>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace aerie_adblock {
namespace {

class FilterListStorageTest : public testing::Test {
 protected:
  void SetUp() override { ASSERT_TRUE(dir_.CreateUniqueTempDir()); }
  base::FilePath dir() const { return dir_.GetPath().AppendASCII("aerie"); }
  base::ScopedTempDir dir_;
};

TEST_F(FilterListStorageTest, MissingOrBrokenFileGivesDefaults) {
  EXPECT_EQ(filter_list_storage::LoadState(dir()).lists().size(), 2u);
  ASSERT_TRUE(base::CreateDirectory(dir()));
  for (const char* junk : {"", "not json", "[1,2]", "{\"lists\": 5}"}) {
    ASSERT_TRUE(base::WriteFile(dir().AppendASCII("state.json"), junk));
    FilterListState state = filter_list_storage::LoadState(dir());
    EXPECT_EQ(state.lists().size(), 2u) << junk;
    EXPECT_TRUE(state.exceptions().empty());
  }
}

TEST_F(FilterListStorageTest, StateSurvivesARestart) {
  FilterListState state;
  state.SetException("news.test", true);
  std::string id;
  state.AddCustomList(GURL("https://lists.example/a.txt"), false, &id);
  ASSERT_TRUE(filter_list_storage::SaveState(dir(), state));

  FilterListState back = filter_list_storage::LoadState(dir());
  EXPECT_EQ(back.exceptions(), state.exceptions());
  EXPECT_TRUE(back.Find(id));
}

TEST_F(FilterListStorageTest, ListsAreWrittenReadAndDeletedUnderTheirId) {
  ASSERT_TRUE(filter_list_storage::WriteList(dir(), "cabc", "||a.test^\n"));
  std::string text;
  ASSERT_TRUE(base::ReadFileToString(filter_list_storage::ListPath(dir(), "cabc"), &text));
  EXPECT_EQ(text, "||a.test^\n");
  filter_list_storage::DeleteList(dir(), "cabc");
  EXPECT_FALSE(base::PathExists(filter_list_storage::ListPath(dir(), "cabc")));
}

TEST_F(FilterListStorageTest, AnInvalidIdNeverMakesAPath) {
  for (const char* bad : {"", "../state", "a/b", "A", "x.y"}) {
    EXPECT_TRUE(filter_list_storage::ListPath(dir(), bad).empty()) << bad;
    EXPECT_FALSE(filter_list_storage::WriteList(dir(), bad, "x")) << bad;
  }
}

}  // namespace
}  // namespace aerie_adblock
