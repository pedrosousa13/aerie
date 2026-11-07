// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_ADBLOCK_BROWSER_COSMETIC_SCRIPTS_H_
#define COMPONENTS_AERIE_ADBLOCK_BROWSER_COSMETIC_SCRIPTS_H_

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "base/values.h"
#include "content/public/common/isolated_world_ids.h"

namespace aerie_adblock {

// The scripts the browser runs in a frame's isolated world to apply cosmetic
// filtering. They are Aerie's own text. The only input a page can influence is
// the class names and ids that CollectClassesAndIdsScript() reads from its
// DOM, and those never reach a script: they only go through
// ParsePageTokens() into the Filter Engine as lookup keys. The stylesheet that
// goes into a script comes from the engine (selectors from trusted lists,
// checked in Rust) and is embedded as a JSON string, so it is data, never
// code.
//
// The world is the one that content reserves for embedders and that Chrome's
// own list (chrome_isolated_world_ids.h) does not use: the page's own scripts
// share the DOM with it but not its JavaScript objects.
inline constexpr int32_t kCosmeticWorldId =
    content::ISOLATED_WORLD_ID_CONTENT_END;

// Script that adds |stylesheet| to the document as a constructed stylesheet,
// which page Content-Security-Policy `style-src` does not govern and which
// changes no DOM node. Empty string when |stylesheet| is empty or not ASCII.
std::u16string BuildInjectionScript(std::string_view stylesheet);

// Script whose result is [classes, ids]: the class names and the ids in the
// document, each a space separated string, at most 5000 of each.
std::u16string_view CollectClassesAndIdsScript();

struct PageTokens {
  std::string classes;
  std::string ids;
};

// Reads the result of CollectClassesAndIdsScript(). nullopt if it is not a
// list of two strings or one is bigger than the allowed size (the page
// controls them).
std::optional<PageTokens> ParsePageTokens(const base::Value& result);

}  // namespace aerie_adblock

#endif  // COMPONENTS_AERIE_ADBLOCK_BROWSER_COSMETIC_SCRIPTS_H_
