// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_ADBLOCK_COMMON_COSMETIC_LIMITS_H_
#define COMPONENTS_AERIE_ADBLOCK_COMMON_COSMETIC_LIMITS_H_

#include <cstddef>
#include <cstdint>

#include "content/public/common/isolated_world_ids.h"

namespace aerie_adblock {

// The one place for every number that bounds cosmetic filtering, in the
// browser (scripts, tab helper) and in the renderer piece (ADR 0005). Nothing
// else declares these. tests/test_cosmetic_limits.py checks the ones that the
// Rust core also has against rust/core/src/lib.rs.

// The isolated world that the browser's cosmetic scripts run in. It is the one
// that content reserves for embedders and that Chrome's own list
// (chrome_isolated_world_ids.h) does not use: the page's own scripts share the
// DOM with it but not its JavaScript objects. The renderer installs its one
// native function in this world only.
inline constexpr int32_t kCosmeticWorldId =
    content::ISOLATED_WORLD_ID_CONTENT_END;

// What the collect script reads from a page. These must equal the limits of
// the Rust core (`MAX_PAGE_TOKENS` and `MAX_TOKEN_BYTES` in
// rust/core/src/lib.rs), which ignores the rest.
inline constexpr size_t kMaxPageTokens = 5000;
inline constexpr size_t kMaxTokenBytes = 256;
// Most elements a script looks at, so a huge DOM cannot stall the frame.
inline constexpr size_t kMaxScannedElements = 50000;

// The biggest stylesheet the renderer accepts from the browser. Must equal
// the core's `MAX_COSMETIC_CSS_BYTES`.
inline constexpr size_t kMaxSheetBytes = 1024 * 1024;

// Each token is at most kMaxTokenBytes bytes, plus a separator. The page
// decides what is in them, so the browser bounds them.
inline constexpr size_t kMaxTokenListBytes =
    kMaxPageTokens * (kMaxTokenBytes + 1);

// Late elements (issue #68). Counts and sizes are size_t; durations are int
// milliseconds. The watch script reports only tokens it has not reported
// before, so a report is small; these bound a hostile page anyway.
//
// The most a single report may hold, for each of the two lists. The renderer
// drops a bigger one and the browser refuses it.
inline constexpr size_t kMaxLateTokenListBytes = 16 * 1024;
// Reports per document. After this the renderer or the browser ignores them.
inline constexpr size_t kMaxLateBatchesPerDocument = 200;
// The renderer sends at most one report per this many milliseconds. The
// browser's gate for a frame has half of it, because messages do not arrive
// evenly.
inline constexpr int kLateMinIntervalMs = 200;
inline constexpr int kLateFrameGateIntervalMs = kLateMinIntervalMs / 2;
// The browser also limits all the frames of a tab together with a token bucket:
// a burst of kLateTabBurst reports (far above what many honest frames send at
// once, each frame sending one per kLateMinIntervalMs at most), then
// one more every kLateTabRefillIntervalMs. It has no lifetime cap, so a frame
// that reloads itself in a loop can only slow the others down for the
// moment, never use up their share for good. The per-frame gate is what bounds
// a single frame; the bucket bounds the tab's total rate.
inline constexpr size_t kLateTabBurst = 64;
inline constexpr int kLateTabRefillIntervalMs = 25;
// The watch script waits this long after a mutation before it reports, so
// one burst of insertions is one report. It must be above kLateMinIntervalMs.
inline constexpr int kLateFlushDelayMs = 250;
// When the renderer does not accept a report the script keeps its tokens and
// tries again after this long, at most kMaxLateFlushRetries times in a row.
inline constexpr int kLateRetryDelayMs = 500;
inline constexpr int kMaxLateFlushRetries = 20;
// Work the watch script does at once for the mutations it has queued: records
// and elements read. What is left waits for the next turn (a timer
// kLateFlushDelayMs later). The cost is bounded, and a page's junk is fair to
// an ad's class only within the limits here: a hostile page can exhaust the
// queue of waiting records, the seen-token limit, the reports and the
// insertions of a document, and then nothing more of it is hidden.
inline constexpr size_t kMaxMutationRecordsPerBatch = 200;
inline constexpr size_t kMaxElementsPerMutationBatch = 2000;
inline constexpr size_t kMaxLateBacklogItems = 5000;
// Classes read from one element, and elements read for a whole document,
// by the watch script. A page can have a long class attribute and can mutate
// without end; neither makes the watch cost more than this.
inline constexpr size_t kMaxClassesPerElement = 32;
inline constexpr size_t kMaxLateElementsPerDocument = 200000;
// Distinct tokens the watch remembers (reported or waiting), for each list.
inline constexpr size_t kMaxLateSeenTokens = kMaxPageTokens;

// The renderer puts the late rules in the frame's one generic sheet by removing
// the sheet and inserting it again, which makes Blink restyle. So an append
// waits until this long after the last insertion, and a document gets at most
// kMaxGenericInsertsPerDocument insertions (the first, at DOM ready, is
// immediate).
inline constexpr int kGenericReinsertMinIntervalMs = 250;
inline constexpr size_t kMaxGenericInsertsPerDocument = 64;

}  // namespace aerie_adblock

#endif  // COMPONENTS_AERIE_ADBLOCK_COMMON_COSMETIC_LIMITS_H_
