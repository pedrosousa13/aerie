// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_adblock/browser/protection_engine.h"

#include <cstdint>
#include <vector>

#include "testing/gtest/include/gtest/gtest.h"

namespace aerie_adblock {
namespace {

TEST(ProtectionEngineTest, EmptyEngineListsNothing) {
  ProtectionEngine engine;
  EXPECT_FALSE(engine.IsListed("evil.example"));
  EXPECT_EQ(0u, engine.domain_count());
}

TEST(ProtectionEngineTest, ListsDomainsAndTheirSubdomainsOnLabelBoundaries) {
  ProtectionEngine engine;
  engine.LoadListForTesting("evil.example\n0.0.0.0 bad.example\n");
  EXPECT_TRUE(engine.IsListed("evil.example"));
  EXPECT_TRUE(engine.IsListed("www.evil.example"));
  EXPECT_TRUE(engine.IsListed("bad.example"));
  EXPECT_FALSE(engine.IsListed("notevil.example"));
  EXPECT_FALSE(engine.IsListed("evil.example.attacker.test"));
  EXPECT_FALSE(engine.IsListed(""));
  EXPECT_EQ(2u, engine.domain_count());
}

TEST(ProtectionEngineTest, NonAsciiHostsAreNotListedAndDoNotThrow) {
  ProtectionEngine engine;
  engine.LoadListForTesting("evil.example\n");
  EXPECT_FALSE(engine.IsListed("\xff\xfe.evil.example"));
}

TEST(ProtectionEngineTest, SerializedEngineRoundTripsAndJunkKeepsTheOldDomains) {
  ProtectionEngine source;
  source.LoadListForTesting("evil.example\n");
  std::vector<uint8_t> bytes = source.SerializeForTesting();

  ProtectionEngine loaded;
  EXPECT_TRUE(loaded.LoadSerialized(bytes));
  EXPECT_TRUE(loaded.IsListed("a.evil.example"));

  const uint8_t junk[] = {'n', 'o', 'p', 'e'};
  EXPECT_FALSE(loaded.LoadSerialized(junk));
  EXPECT_TRUE(loaded.IsListed("evil.example"));
}

}  // namespace
}  // namespace aerie_adblock
