// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_adblock/browser/adblock_engine.h"

#include <cstdint>
#include <string>
#include <vector>

#include "testing/gtest/include/gtest/gtest.h"

namespace aerie_adblock {
namespace {

ClassifiedRequest Script(const char* url, const char* host) {
  return ClassifiedRequest{url, host, "news.test", RequestKind::kScript, true};
}

TEST(AdblockEngineTest, EmptyEngineBlocksNothing) {
  AdblockEngine engine;
  EXPECT_FALSE(engine.ShouldBlock(Script("https://ads.example/a.js", "ads.example")));
}

TEST(AdblockEngineTest, BlocksListedUrlAndHonoursTypeAndExceptions) {
  AdblockEngine engine;
  engine.LoadListForTesting("||ads.example^$script\n@@||ads.example/ok.js\n");
  EXPECT_TRUE(engine.ShouldBlock(Script("https://ads.example/a.js", "ads.example")));
  EXPECT_FALSE(engine.ShouldBlock(Script("https://ads.example/ok.js", "ads.example")));
  ClassifiedRequest image = Script("https://ads.example/a.png", "ads.example");
  image.kind = RequestKind::kImage;
  EXPECT_FALSE(engine.ShouldBlock(image));
}

TEST(AdblockEngineTest, FirstPartyContentIsAllowed) {
  AdblockEngine engine;
  engine.LoadListForTesting("||news.test^$third-party\n");
  ClassifiedRequest first_party{"https://news.test/app.js", "news.test", "news.test",
                                RequestKind::kScript, false};
  EXPECT_FALSE(engine.ShouldBlock(first_party));
  first_party.source_hostname = "other.test";
  first_party.third_party = true;
  EXPECT_TRUE(engine.ShouldBlock(first_party));
}

TEST(AdblockEngineTest, NonAsciiRequestIsNeverPassedToRust) {
  AdblockEngine engine;
  engine.LoadListForTesting("||ads.example^\n");
  EXPECT_FALSE(engine.ShouldBlock(Script("https://ads.example/\xc3\xa9", "ads.example")));
}

TEST(AdblockEngineTest, CosmeticStylesheetHidesSiteRulesAndComplexGenerics) {
  AdblockEngine engine;
  engine.LoadListForTesting(
      "news.test##.site-ad\n##.ad-banner\n##div[class*=\"Ads\"]\n");
  std::string css = engine.CosmeticStylesheet("https://news.test/a");
  EXPECT_NE(css.find(".site-ad{display:none!important}"), std::string::npos);
  EXPECT_NE(css.find("div[class*=\"Ads\"]{display:none!important}"),
            std::string::npos);
  EXPECT_EQ(engine.CosmeticStylesheet("https://other.test/").find(".site-ad"),
            std::string::npos);
  EXPECT_TRUE(engine.CosmeticStylesheet("chrome://settings/").empty());
}

TEST(AdblockEngineTest, GenericStylesheetNeedsAMatchingClassOrId) {
  AdblockEngine engine;
  engine.LoadListForTesting("##.ad-banner\n###ad-slot\n");
  EXPECT_EQ(engine.GenericStylesheet("https://news.test/", "ad-banner x", ""),
            ".ad-banner{display:none!important}\n");
  EXPECT_EQ(engine.GenericStylesheet("https://news.test/", "", "ad-slot"),
            "#ad-slot{display:none!important}\n");
  EXPECT_TRUE(engine.GenericStylesheet("https://news.test/", "x", "y").empty());
}

TEST(AdblockEngineTest, NonAsciiCosmeticInputIsNeverPassedToRust) {
  AdblockEngine engine;
  engine.LoadListForTesting("##.ad-banner\n");
  EXPECT_TRUE(engine.CosmeticStylesheet("https://news.test/\xc3\xa9").empty());
  EXPECT_TRUE(
      engine.GenericStylesheet("https://news.test/", "ad-banner\xc3\xa9", "")
          .empty());
}

TEST(AdblockEngineTest, SerializedRoundTripKeepsRules) {
  AdblockEngine source;
  source.LoadListForTesting("||ads.example^$script\n");
  std::vector<uint8_t> bytes = source.SerializeForTesting();
  ASSERT_FALSE(bytes.empty());

  AdblockEngine loaded;
  ASSERT_TRUE(loaded.LoadSerialized(bytes));
  EXPECT_TRUE(loaded.ShouldBlock(Script("https://ads.example/a.js", "ads.example")));
}

TEST(AdblockEngineTest, BadSerializedEngineIsRefusedAndKeepsOldRules) {
  AdblockEngine engine;
  engine.LoadListForTesting("||ads.example^\n");
  const std::vector<uint8_t> junk = {1, 2, 3, 4};
  EXPECT_FALSE(engine.LoadSerialized(junk));
  EXPECT_FALSE(engine.LoadSerialized(std::vector<uint8_t>()));
  EXPECT_TRUE(engine.ShouldBlock(Script("https://ads.example/a.js", "ads.example")));
}

// Malformed lists must not crash. (The Rust tests go deeper: garbage bytes,
// huge lines, regex bombs. This checks the C++ path.)
TEST(AdblockEngineTest, MalformedListsDoNotCrash) {
  AdblockEngine engine;
  engine.LoadListForTesting(std::string_view("\xff\xfe\0||x^\n@@\n/(((/\n$$$\n", 21));
  engine.LoadListForTesting("");
  engine.LoadListForTesting(std::string(1 << 20, 'a'));
  EXPECT_FALSE(engine.ShouldBlock(Script("https://ads.example/a.js", "ads.example")));
}

}  // namespace
}  // namespace aerie_adblock
