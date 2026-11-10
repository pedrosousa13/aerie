// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_adblock/browser/adblock_engine.h"

#include <cstdint>
#include <string>
#include <vector>
#include <string>
#include <string_view>

#include "base/containers/span.h"
#include "base/time/time.h"
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

// A first-party request that an EasyList-style rule matches (issue #109).
ClassifiedRequest FirstPartyScript(const char* url, const char* host) {
  return ClassifiedRequest{url, host, host, RequestKind::kScript, false};
}

TEST(AdblockEngineTest, AggressiveBlocksFirstPartyMatches) {
  AdblockEngine engine;
  engine.LoadListForTesting("/ads/banner.js\n");
  const ClassifiedRequest request =
      FirstPartyScript("https://news.test/ads/banner.js", "news.test");
  EXPECT_TRUE(engine.ShouldBlock(request, BlockingLevel::kAggressive));
  // The default is Aggressive: today's behaviour.
  EXPECT_TRUE(engine.ShouldBlock(request));
}

TEST(AdblockEngineTest, StandardAllowsFirstPartyMatches) {
  AdblockEngine engine;
  engine.LoadListForTesting("/ads/banner.js\n");
  EXPECT_FALSE(engine.ShouldBlock(
      FirstPartyScript("https://news.test/ads/banner.js", "news.test"),
      BlockingLevel::kStandard));
}

TEST(AdblockEngineTest, StandardStillBlocksThirdPartyMatches) {
  AdblockEngine engine;
  engine.LoadListForTesting("/ads/banner.js\n||ads.example^\n");
  ClassifiedRequest request{"https://ads.example/ads/banner.js", "ads.example",
                            "news.test", RequestKind::kScript, true};
  EXPECT_TRUE(engine.ShouldBlock(request, BlockingLevel::kStandard));
  EXPECT_TRUE(engine.ShouldBlock(request, BlockingLevel::kAggressive));
}

TEST(AdblockEngineTest, ImportantFollowsBraveCoreInStandard) {
  AdblockEngine engine;
  // brave-core drops the default lists' verdict, `$important` rules included,
  // for a first-party request outside Aggressive, and keeps it for the rest.
  engine.LoadListForTesting(
      "||news.test/ads/$important\n@@||news.test^\n||ads.example^$important\n"
      "@@||ads.example^\n");
  const ClassifiedRequest first_party =
      FirstPartyScript("https://news.test/ads/a.js", "news.test");
  EXPECT_TRUE(engine.ShouldBlock(first_party, BlockingLevel::kAggressive));
  EXPECT_FALSE(engine.ShouldBlock(first_party, BlockingLevel::kStandard));
  ClassifiedRequest third_party{"https://ads.example/a.js", "ads.example",
                                "news.test", RequestKind::kScript, true};
  EXPECT_TRUE(engine.ShouldBlock(third_party, BlockingLevel::kAggressive));
  EXPECT_TRUE(engine.ShouldBlock(third_party, BlockingLevel::kStandard));
}

TEST(AdblockEngineTest, StandardStillJudgesFirstPartyByCustomLists) {
  AdblockEngine engine;
  // brave-core's second engine: a custom list's rules, `$important` ones
  // included, apply to first-party requests in Standard. A built-in list's do
  // not.
  ASSERT_EQ(ListFailure::kNone,
            engine.LoadListsForTesting(
                "||news.test/builtin/$important\n",
                "||news.test/custom/$important\n/ads/own.js\n"));
  const ClassifiedRequest built_in =
      FirstPartyScript("https://news.test/builtin/a.js", "news.test");
  const ClassifiedRequest custom =
      FirstPartyScript("https://news.test/custom/a.js", "news.test");
  const ClassifiedRequest custom_plain =
      FirstPartyScript("https://news.test/ads/own.js", "news.test");
  EXPECT_TRUE(engine.ShouldBlock(built_in, BlockingLevel::kAggressive));
  EXPECT_FALSE(engine.ShouldBlock(built_in, BlockingLevel::kStandard));
  EXPECT_TRUE(engine.ShouldBlock(custom, BlockingLevel::kAggressive));
  EXPECT_TRUE(engine.ShouldBlock(custom, BlockingLevel::kStandard));
  EXPECT_TRUE(engine.ShouldBlock(custom_plain, BlockingLevel::kStandard));
}

TEST(AdblockEngineTest, ABadListKeepsTheOldFirstPartyEngine) {
  AdblockEngine engine;
  ASSERT_EQ(ListFailure::kNone,
            engine.LoadListsForTesting("||other.example^\n", "/ads/own.js\n"));
  const ClassifiedRequest request =
      FirstPartyScript("https://news.test/ads/own.js", "news.test");
  EXPECT_TRUE(engine.ShouldBlock(request, BlockingLevel::kStandard));
  EXPECT_NE(ListFailure::kNone,
            engine.LoadLists({"<html>not a list</html>"}, base::Seconds(30),
                             {true})
                .failure);
  EXPECT_TRUE(engine.ShouldBlock(request, BlockingLevel::kStandard));
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
      engine.GenericStylesheet("https://news.test/\xc3\xa9", "ad-banner", "")
          .empty());
  // Only the token that is not ASCII is skipped, never the whole list.
  EXPECT_TRUE(
      engine.GenericStylesheet("https://news.test/", "ad-banner\xc3\xa9", "")
          .empty());
}

// One bad token in a report must not void the others (issue #68 review).
TEST(AdblockEngineTest, NonAsciiTokensAreSkippedOneAtATime) {
  AdblockEngine engine;
  engine.LoadListForTesting("##.ad-banner\n###ad-slot\n");
  EXPECT_EQ(engine.GenericStylesheet("https://news.test/",
                                     "caf\xc3\xa9 ad-banner \xe2\x82\xac", ""),
            ".ad-banner{display:none!important}\n");
  EXPECT_EQ(engine.GenericStylesheet("https://news.test/", "x",
                                     "\xc3\xa9 ad-slot"),
            "#ad-slot{display:none!important}\n");
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

TEST(AdblockEngineListsTest, CheckListAcceptsAListAndNamesTheFailures) {
  auto check = [](std::string_view text) {
    return CheckList(base::as_byte_span(text), base::Seconds(5));
  };
  ListCheck good = check("! Title: Mine\n||ads.example^\n||b.example^\n");
  EXPECT_EQ(good.failure, ListFailure::kNone);
  EXPECT_EQ(good.rules, 2);
  EXPECT_EQ(good.title, "Mine");
  EXPECT_EQ(check("<html>404</html>").failure, ListFailure::kLooksLikeHtml);
  EXPECT_EQ(check("! only a comment\n").failure, ListFailure::kNoRules);
  EXPECT_EQ(check(std::string_view("||a.test^\0\n", 11)).failure, ListFailure::kNotText);
  EXPECT_EQ(CheckList(base::as_byte_span(std::string_view("||a.test^\n")),
                      base::TimeDelta())
                .failure,
            ListFailure::kTimeout);
}

TEST(AdblockEngineListsTest, LoadListsReplacesTheRulesOnlyWhenAllListsAreGood) {
  AdblockEngine engine;
  engine.LoadListForTesting("||old.example^\n");
  ASSERT_TRUE(engine.ShouldBlock(Script("https://old.example/a.js", "old.example")));

  BuildResult ok = engine.LoadLists({"||one.example^\n", "||two.example^\n"},
                                    base::Seconds(5));
  EXPECT_EQ(ok.failure, ListFailure::kNone);
  EXPECT_TRUE(engine.ShouldBlock(Script("https://one.example/a.js", "one.example")));
  EXPECT_TRUE(engine.ShouldBlock(Script("https://two.example/a.js", "two.example")));
  EXPECT_FALSE(engine.ShouldBlock(Script("https://old.example/a.js", "old.example")));

  BuildResult bad = engine.LoadLists({"||new.example^\n", "<html>"}, base::Seconds(5));
  EXPECT_EQ(bad.failure, ListFailure::kLooksLikeHtml);
  EXPECT_EQ(bad.failed_index, 1u);
  EXPECT_TRUE(engine.ShouldBlock(Script("https://one.example/a.js", "one.example")));
  EXPECT_FALSE(engine.ShouldBlock(Script("https://new.example/a.js", "new.example")));
}

}  // namespace
}  // namespace aerie_adblock
