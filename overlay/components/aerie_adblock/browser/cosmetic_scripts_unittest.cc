// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_adblock/browser/cosmetic_scripts.h"

#include <string>

#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace aerie_adblock {
namespace {

TEST(CosmeticScriptsTest, NoStylesheetNoScript) {
  EXPECT_TRUE(BuildInjectionScript("").empty());
  EXPECT_TRUE(BuildInjectionScript("\xc3\xa9{display:none}").empty());
}

TEST(CosmeticScriptsTest, StylesheetIsEmbeddedAsAJsonStringLiteral) {
  std::string script = base::UTF16ToUTF8(
      BuildInjectionScript(".ad{display:none!important}\n"));
  EXPECT_NE(script.find("replaceSync(css)"), std::string::npos);
  EXPECT_NE(script.find("(\".ad{display:none!important}\\n\");"),
            std::string::npos);
}

// Whatever the text holds, it cannot close the string literal and run as code.
TEST(CosmeticScriptsTest, HostileTextStaysInsideTheLiteral) {
  const std::string hostile = "\");alert(1);//\\\"</script><script>x";
  std::string script = base::UTF16ToUTF8(BuildInjectionScript(hostile));
  EXPECT_EQ(script.find("alert(1);//\\\"</script>"), std::string::npos);
  EXPECT_EQ(script.find("</script>"), std::string::npos);
  EXPECT_NE(script.find("\\\");alert(1);//\\\\\\\""), std::string::npos);
}

TEST(CosmeticScriptsTest, CollectScriptReturnsTwoStrings) {
  std::string script = base::UTF16ToUTF8(std::u16string(CollectClassesAndIdsScript()));
  EXPECT_NE(script.find("querySelectorAll('[class],[id]')"), std::string::npos);
  EXPECT_NE(script.find("slice(0,5000)"), std::string::npos);
  // It reads the DOM only: no network, no eval, no writes.
  for (const char* banned : {"fetch", "eval", "XMLHttpRequest", "innerHTML",
                             "appendChild", "Function("}) {
    EXPECT_EQ(script.find(banned), std::string::npos) << banned;
  }
}

TEST(CosmeticScriptsTest, ParsePageTokensAcceptsOnlyTwoStrings) {
  base::ListValue good;
  good.Append("a b");
  good.Append("x");
  auto tokens = ParsePageTokens(base::Value(std::move(good)));
  ASSERT_TRUE(tokens.has_value());
  EXPECT_EQ(tokens->classes, "a b");
  EXPECT_EQ(tokens->ids, "x");

  EXPECT_FALSE(ParsePageTokens(base::Value()).has_value());
  EXPECT_FALSE(ParsePageTokens(base::Value("a b")).has_value());
  base::ListValue one;
  one.Append("a");
  EXPECT_FALSE(ParsePageTokens(base::Value(std::move(one))).has_value());
  base::ListValue wrong;
  wrong.Append(1);
  wrong.Append("x");
  EXPECT_FALSE(ParsePageTokens(base::Value(std::move(wrong))).has_value());
}

TEST(CosmeticScriptsTest, ParsePageTokensBoundsWhatThePageControls) {
  base::ListValue huge;
  huge.Append(std::string(5000 * 257 + 1, 'a'));
  huge.Append("x");
  EXPECT_FALSE(ParsePageTokens(base::Value(std::move(huge))).has_value());
}

}  // namespace
}  // namespace aerie_adblock
