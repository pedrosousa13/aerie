// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_adblock/browser/session_key.h"

#include <map>

#include "testing/gtest/include/gtest/gtest.h"

namespace aerie_adblock {

TEST(SessionKeyTest, RegularIsNotOffTheRecord) {
  EXPECT_TRUE(SessionKey::Regular().is_regular());
  EXPECT_FALSE(SessionKey::Regular().is_off_the_record());
}

TEST(SessionKeyTest, AnOffTheRecordKeyIsItsProfileOnly) {
  int a = 0, b = 0;
  SessionKey key_a = SessionKey::OffTheRecord(&a);
  EXPECT_TRUE(key_a.is_off_the_record());
  EXPECT_FALSE(key_a.is_regular());
  EXPECT_EQ(key_a, SessionKey::OffTheRecord(&a));
  EXPECT_NE(key_a, SessionKey::OffTheRecord(&b));
  EXPECT_NE(key_a, SessionKey::Regular());
}

TEST(SessionKeyTest, KeysWorkAsMapKeys) {
  int a = 0, b = 0;
  std::map<SessionKey, int> map;
  map[SessionKey::OffTheRecord(&a)] = 1;
  map[SessionKey::OffTheRecord(&b)] = 2;
  map[SessionKey::Regular()] = 3;
  EXPECT_EQ(3u, map.size());
  EXPECT_EQ(2, map[SessionKey::OffTheRecord(&b)]);
}

}  // namespace aerie_adblock
