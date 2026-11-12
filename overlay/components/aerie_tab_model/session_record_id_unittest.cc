// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_tab_model/session_record_id.h"

#include <string>

#include "testing/gtest/include/gtest/gtest.h"

namespace aerie_tab_model {

TEST(SessionRecordIdTest, AUuidIsAnId) {
  const std::string uuid = "0f8fad5b-d9cb-469f-a165-70867728950e";
  EXPECT_EQ(ParseSessionRecordId(uuid), uuid);
}

TEST(SessionRecordIdTest, ShortTestIdsAreIds) {
  EXPECT_EQ(ParseSessionRecordId("id-7"), "id-7");
  EXPECT_EQ(ParseSessionRecordId("a_B-9"), "a_B-9");
}

TEST(SessionRecordIdTest, AnEmptyValueIsNoId) {
  EXPECT_EQ(ParseSessionRecordId(""), std::nullopt);
}

TEST(SessionRecordIdTest, ATooLongValueIsNoId) {
  EXPECT_TRUE(ParseSessionRecordId(std::string(kMaxSessionRecordIdLength, 'a')));
  EXPECT_EQ(ParseSessionRecordId(std::string(kMaxSessionRecordIdLength + 1, 'a')),
            std::nullopt);
  EXPECT_EQ(ParseSessionRecordId(std::string(1 << 20, 'a')), std::nullopt);
}

TEST(SessionRecordIdTest, OtherCharactersAreNoId) {
  for (const char* bad :
       {"has space", "a/b", "a.b", "a\"b", "<script>", "../x", "a\nb", "\xc3\xa9"}) {
    EXPECT_EQ(ParseSessionRecordId(bad), std::nullopt) << bad;
  }
  // An embedded NUL is not a terminator.
  EXPECT_EQ(ParseSessionRecordId(std::string("ab\0cd", 5)), std::nullopt);
}

}  // namespace aerie_tab_model
