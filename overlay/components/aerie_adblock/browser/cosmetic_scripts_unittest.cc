// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_adblock/browser/cosmetic_scripts.h"

#include <string>

#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace aerie_adblock {
namespace {

TEST(CosmeticScriptsTest, CollectScriptReturnsTwoStrings) {
  std::string script = base::UTF16ToUTF8(CollectClassesAndIdsScript());
  EXPECT_NE(script.find("querySelectorAll('[class],[id]')"), std::string::npos);
  // It reads the DOM only: no network, no eval, no writes.
  for (const char* banned : {"fetch", "eval", "XMLHttpRequest", "innerHTML",
                             "appendChild", "Function("}) {
    EXPECT_EQ(script.find(banned), std::string::npos) << banned;
  }
}

// The script's numbers come from the constants. tests/test_cosmetic_limits.py
// checks the constants against the Rust core.
TEST(CosmeticScriptsTest, CollectScriptUsesTheTokenLimits) {
  std::string script = base::UTF16ToUTF8(CollectClassesAndIdsScript());
  const std::string tokens = base::NumberToString(kMaxPageTokens);
  EXPECT_NE(script.find("slice(0," + tokens + ").join"), std::string::npos);
  EXPECT_NE(script.find("n<" + base::NumberToString(kMaxScannedElements) + ";"),
            std::string::npos);
}

TEST(CosmeticScriptsTest, LateWatchIsInstalledOnce) {
  std::string script = base::UTF16ToUTF8(LateWatchScript());
  EXPECT_NE(script.find("if(globalThis.__aerieLateWatch){return;}"),
            std::string::npos);
  // Without the renderer's native function there is nothing to call, so it
  // does nothing.
  EXPECT_NE(script.find("typeof notify!=='function'"), std::string::npos);
}

TEST(CosmeticScriptsTest, LateWatchOnlyReadsTheDomAndCallsTheNativeFunction) {
  std::string script = base::UTF16ToUTF8(LateWatchScript());
  EXPECT_NE(script.find("new MutationObserver("), std::string::npos);
  EXPECT_NE(script.find("globalThis.__aerieCosmeticNotify"),
            std::string::npos);
  for (const char* banned :
       {"fetch", "eval(", "XMLHttpRequest", "innerHTML", "appendChild",
        "Function(", "setAttribute", "style.", "remove("}) {
    EXPECT_EQ(script.find(banned), std::string::npos) << banned;
  }
}

// Every number in the script comes from cosmetic_limits.h.
TEST(CosmeticScriptsTest, LateWatchIsBoundedByTheLimits) {
  std::string script = base::UTF16ToUTF8(LateWatchScript());
  auto has = [&](const std::string& text) {
    return script.find(text) != std::string::npos;
  };
  EXPECT_TRUE(
      has("room=" + base::NumberToString(kMaxElementsPerMutationBatch)));
  EXPECT_TRUE(
      has("recs=" + base::NumberToString(kMaxMutationRecordsPerBatch)));
  EXPECT_TRUE(
      has("left=" + base::NumberToString(kMaxLateElementsPerDocument)));
  EXPECT_TRUE(
      has("work.length<" + base::NumberToString(kMaxLateBacklogItems)));
  EXPECT_TRUE(has("k.size<" + base::NumberToString(kMaxLateSeenTokens)));
  EXPECT_TRUE(has("j<" + base::NumberToString(kMaxClassesPerElement)));
  EXPECT_TRUE(has("fails>" + base::NumberToString(kMaxLateFlushRetries)));
  EXPECT_TRUE(
      has("setTimeout(drain," + base::NumberToString(kLateFlushDelayMs)));
  EXPECT_TRUE(
      has("setTimeout(flush," + base::NumberToString(kLateFlushDelayMs)));
}

// A bad token must not void a report, and the engine takes ASCII only.
TEST(CosmeticScriptsTest, LateWatchDropsNonAsciiTokens) {
  std::string script = base::UTF16ToUTF8(LateWatchScript());
  EXPECT_NE(script.find("/[^\\x00-\\x7f]/.test(t)"), std::string::npos);
}

// A token leaves its queue only after the renderer accepted the report, and a
// flood of mutations waits in a queue instead of being dropped.
TEST(CosmeticScriptsTest, LateWatchKeepsTokensUntilTheReportIsAccepted) {
  std::string script = base::UTF16ToUTF8(LateWatchScript());
  EXPECT_NE(script.find("===true"), std::string::npos);
  EXPECT_NE(script.find("if(sent){fails=0;qc.splice(0,nc);qi.splice(0,ni);}"),
            std::string::npos);
  EXPECT_NE(script.find("work.push("), std::string::npos);
  EXPECT_NE(script.find("work.shift()"), std::string::npos);
}

TEST(CosmeticScriptsTest, FlushDelayIsAboveTheRenderersMinimumInterval) {
  EXPECT_GT(kLateFlushDelayMs, kLateMinIntervalMs);
}

TEST(CosmeticScriptsTest, ParsePageTokensTakesTwoStrings) {
  base::ListValue ok;
  ok.Append("a b");
  ok.Append("c");
  std::optional<PageTokens> tokens =
      ParsePageTokens(base::Value(std::move(ok)));
  ASSERT_TRUE(tokens.has_value());
  EXPECT_EQ(tokens->classes, "a b");
  EXPECT_EQ(tokens->ids, "c");
}

TEST(CosmeticScriptsTest, ParsePageTokensRefusesOtherShapes) {
  base::ListValue one;
  one.Append("a");
  EXPECT_FALSE(ParsePageTokens(base::Value(std::move(one))).has_value());

  base::ListValue three;
  three.Append("a");
  three.Append("b");
  three.Append("c");
  EXPECT_FALSE(ParsePageTokens(base::Value(std::move(three))).has_value());

  base::ListValue not_a_string;
  not_a_string.Append("a");
  not_a_string.Append(1);
  EXPECT_FALSE(
      ParsePageTokens(base::Value(std::move(not_a_string))).has_value());

  EXPECT_FALSE(ParsePageTokens(base::Value("not a list")).has_value());
}

TEST(CosmeticScriptsTest, ParsePageTokensRefusesAnOversizedList) {
  base::ListValue big;
  big.Append(std::string(kMaxTokenListBytes + 1, 'a'));
  big.Append("c");
  EXPECT_FALSE(ParsePageTokens(base::Value(std::move(big))).has_value());
}

}  // namespace
}  // namespace aerie_adblock
