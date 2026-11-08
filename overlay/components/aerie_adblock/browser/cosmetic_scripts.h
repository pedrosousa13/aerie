// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_ADBLOCK_BROWSER_COSMETIC_SCRIPTS_H_
#define COMPONENTS_AERIE_ADBLOCK_BROWSER_COSMETIC_SCRIPTS_H_

#include <optional>
#include <string>

#include "base/values.h"
#include "components/aerie_adblock/common/cosmetic_limits.h"

namespace aerie_adblock {

// The scripts the browser runs in a frame's isolated world to apply cosmetic
// filtering. They are Aerie's own text. The only input a page can influence is
// the class names and ids that CollectClassesAndIdsScript() reads from its
// DOM, and those never reach a script: they only go through
// ParsePageTokens() into the Filter Engine as lookup keys. No stylesheet text
// is ever put in a script.
//
// The world they run in (kCosmeticWorldId) and every limit on them are in
// common/cosmetic_limits.h, the one place for both the browser and the renderer
// piece (ADR 0005).
//
// They do not add a stylesheet. The browser hands the engine's CSS to the
// renderer's AerieCosmeticAgent over Mojo, which inserts it as a user-origin
// sheet that the page cannot see or remove.

// Script whose result is [classes, ids]: the class names and the ids in the
// document, each a space separated string, at most kMaxPageTokens of each.
std::u16string CollectClassesAndIdsScript();

// Script that watches the document for elements inserted or changed after the
// browser read it (issue #68). It is installed once per document (a second run
// does nothing), and calls the function `__aerieCosmeticNotify(classes, ids)`
// that the renderer's AerieCosmeticAgent puts in the Aerie world, with the
// class names and ids it has not reported before, as two space separated
// strings. One report follows a burst of insertions by kLateFlushDelayMs. The
// work per mutation batch, per report and per document is bounded by the
// limits in cosmetic_limits.h. It only reads the DOM.
std::u16string LateWatchScript();

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
