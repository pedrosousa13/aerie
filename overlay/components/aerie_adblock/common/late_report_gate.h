// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_ADBLOCK_COMMON_LATE_REPORT_GATE_H_
#define COMPONENTS_AERIE_ADBLOCK_COMMON_LATE_REPORT_GATE_H_

#include <cstddef>
#include <optional>
#include <string_view>

#include "base/time/time.h"
#include "components/aerie_adblock/common/cosmetic_limits.h"

namespace aerie_adblock {

// What lets a token report through, on either side of the Mojo pipe (ADR
// 0005). A hostile page controls the watch script's input, so the renderer
// bounds the size and the rate of its reports, and the browser checks again,
// for each frame and for the whole tab. Pure code with unit tests.
class LateReportGate {
 public:
  // |min_interval| is the shortest time between two reports and |max_reports|
  // the most until Reset().
  explicit LateReportGate(
      base::TimeDelta min_interval = base::Milliseconds(kLateMinIntervalMs),
      size_t max_reports = kMaxLateBatchesPerDocument);
  ~LateReportGate();

  // True if a report with these two lists, made at |now|, may be sent. Counts
  // it when it is allowed. False when a list is over kMaxLateTokenListBytes,
  // when both are empty, when the last allowed report was less than
  // |min_interval| ago, or after |max_reports| reports.
  bool Allow(std::string_view classes,
             std::string_view ids,
             base::TimeTicks now);

  void Reset();

 private:
  const base::TimeDelta min_interval_;
  const size_t max_reports_;
  size_t sent_ = 0;
  std::optional<base::TimeTicks> last_;
};

// A token bucket: up to |capacity| tokens, one more every |refill_interval|.
// The browser uses one for all the frames of a tab (ADR 0005). Check with
// HasToken() and take with Take() once the report has passed every other
// check, so a report that is refused elsewhere costs the bucket nothing.
class LateTokenBucket {
 public:
  LateTokenBucket(size_t capacity, base::TimeDelta refill_interval);
  ~LateTokenBucket();

  // True if a token is there at |now|.
  bool HasToken(base::TimeTicks now);
  // Takes one token. Call only after HasToken() returned true.
  void Take(base::TimeTicks now);

 private:
  void Refill(base::TimeTicks now);

  const size_t capacity_;
  const base::TimeDelta refill_interval_;
  size_t tokens_;
  std::optional<base::TimeTicks> last_refill_;
};

}  // namespace aerie_adblock

#endif  // COMPONENTS_AERIE_ADBLOCK_COMMON_LATE_REPORT_GATE_H_
