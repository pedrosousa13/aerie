// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_ADBLOCK_RENDERER_COSMETIC_SHEET_STATE_H_
#define COMPONENTS_AERIE_ADBLOCK_RENDERER_COSMETIC_SHEET_STATE_H_

#include <cstddef>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <string_view>

#include "base/time/time.h"
#include "components/aerie_adblock/common/cosmetic.mojom.h"
#include "components/aerie_adblock/common/cosmetic_limits.h"

namespace aerie_adblock {

// The text of a frame's two hide sheets and the checks on what the browser
// sends, with no Blink in it (so it has unit tests). The renderer piece
// (ADR 0005) puts what Apply() returns into the document as a user-origin
// sheet; it adds no selector and does not read the CSS beyond splitting it
// into lines (the engine makes one `selector{display:none!important}` line per
// selector).
//
// A frame has at most one sheet of each kind. kPage: the rules for the page's
// URL, replaced whole. kGeneric: the rules for the page's class names and ids.
// The browser appends to it at DOMContentLoaded, at load and for each batch of
// late elements, in any order, so a line that is already there is not added
// again.
class CosmeticSheetState {
 public:
  CosmeticSheetState();
  ~CosmeticSheetState();

  // Applies one SetRules() call. Returns the whole new text of the sheet of
  // |kind|, to be (re)inserted, or nullopt when there is nothing to insert:
  // the call is refused, or (with |append|) every line was already in the
  // sheet or the sheet is full. Refused: empty text, text that is not ASCII
  // or holds a NUL (the engine produces neither), and text over kMaxSheetBytes.
  // A sheet that would pass kMaxSheetBytes stops at the last line that fits.
  // Empty text with |append| false clears the sheet and returns the empty
  // string: the browser sends it when a page has no rules, so an old sheet
  // does not stay.
  std::optional<std::string> Apply(mojom::CosmeticSheetKind kind,
                                   std::string_view css,
                                   bool append);

  // The whole text of the sheet of |kind| now.
  const std::string& Text(mojom::CosmeticSheetKind kind) const;

  // A new document: nothing of the old one is kept.
  void Reset();

 private:
  struct Sheet {
    Sheet();
    ~Sheet();
    std::string text;
    std::set<std::string, std::less<>> lines;
  };

  Sheet page_;
  Sheet generic_;
};

// When the generic sheet may be inserted again. Inserting removes the old
// sheet and adds the new one, and Blink restyles the document each time, so the
// renderer does it at a bounded rate (ADR 0005): the first time at once, later
// ones at least kGenericReinsertMinIntervalMs after the last, and at most
// kMaxGenericInsertsPerDocument per document. Rules that arrive in between
// wait in CosmeticSheetState and go in with the next insertion. There is still
// exactly one generic sheet.
class GenericInsertPacer {
 public:
  struct Decision {
    enum Kind {
      kNow,    // Insert now, then call OnInserted().
      kLater,  // Insert in |delay|, then call OnInserted().
      kDrop,   // The document has used up its insertions.
    };
    Kind kind;
    base::TimeDelta delay;
  };

  GenericInsertPacer();
  ~GenericInsertPacer();

  // What to do about a change to the generic sheet noticed at |now|.
  Decision OnChange(base::TimeTicks now) const;
  // The sheet was inserted at |now|.
  void OnInserted(base::TimeTicks now);
  // A new document.
  void Reset();

 private:
  size_t inserts_ = 0;
  std::optional<base::TimeTicks> last_;
};

}  // namespace aerie_adblock

#endif  // COMPONENTS_AERIE_ADBLOCK_RENDERER_COSMETIC_SHEET_STATE_H_
