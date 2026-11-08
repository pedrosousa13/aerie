// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_adblock/browser/cosmetic_scripts.h"

#include "base/json/string_escape.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"

namespace aerie_adblock {

namespace {

// The name of the generic sheet in the isolated world's global.
constexpr char kGenericSheetProperty[] = "__aerieGenericSheet";

// Each token is at most kMaxTokenBytes bytes, plus a separator. The page
// decides what is in them, so the browser bounds them.
constexpr size_t kMaxTokenListBytes = kMaxPageTokens * (kMaxTokenBytes + 1);

}  // namespace

std::u16string BuildInjectionScript(std::string_view stylesheet,
                                    SheetKind kind) {
  if (stylesheet.empty() || !base::IsStringASCII(stylesheet)) {
    return std::u16string();
  }
  // GetQuotedJSONString escapes quotes, backslashes, control characters and
  // `<`, `>` and `&`, so the text cannot leave the string literal.
  std::string literal = base::GetQuotedJSONString(stylesheet);
  if (kind == SheetKind::kGeneric) {
    // The sheet lives in the isolated world's global, which the page cannot
    // reach. It is adopted again if the document no longer holds it.
    return base::UTF8ToUTF16(base::StrCat(
        {"(function(css){try{var s=globalThis.", kGenericSheetProperty,
         ";if(!s){s=new CSSStyleSheet();globalThis.", kGenericSheetProperty,
         "=s;}s.replaceSync(css);"
         "if(!document.adoptedStyleSheets.includes(s)){"
         "document.adoptedStyleSheets=[...document.adoptedStyleSheets,s];}"
         "}catch(e){}})(",
         literal, ");"}));
  }
  return base::UTF8ToUTF16(base::StrCat(
      {"(function(css){try{var s=new CSSStyleSheet();s.replaceSync(css);"
       "document.adoptedStyleSheets=[...document.adoptedStyleSheets,s];"
       "}catch(e){}})(",
       literal, ");"}));
}

std::u16string CollectClassesAndIdsScript() {
  const std::u16string max_tokens = base::NumberToString16(kMaxPageTokens);
  return base::StrCat(
      {u"(function(){"
       u"var c=new Set(),d=new Set(),n=0,"
       u"all=document.querySelectorAll('[class],[id]');"
       u"for(var i=0;i<all.length&&n<",
       base::NumberToString16(kMaxScannedElements),
       u";i++,n++){"
       u"var e=all[i];"
       u"if(e.classList){for(var j=0;j<e.classList.length;j++){"
       u"c.add(e.classList[j]);}}"
       u"if(typeof e.id==='string'&&e.id){d.add(e.id);}"
       u"}"
       u"return [Array.from(c).slice(0,",
       max_tokens,
       u").join(' '),"
       u"Array.from(d).slice(0,",
       max_tokens,
       u").join(' ')];"
       u"})()"});
}

std::optional<PageTokens> ParsePageTokens(const base::Value& result) {
  if (!result.is_list()) {
    return std::nullopt;
  }
  const base::ListValue& list = result.GetList();
  if (list.size() != 2 || !list[0].is_string() || !list[1].is_string()) {
    return std::nullopt;
  }
  const std::string& classes = list[0].GetString();
  const std::string& ids = list[1].GetString();
  if (classes.size() > kMaxTokenListBytes || ids.size() > kMaxTokenListBytes) {
    return std::nullopt;
  }
  return PageTokens{classes, ids};
}

}  // namespace aerie_adblock
