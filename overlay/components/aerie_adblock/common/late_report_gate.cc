// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_adblock/common/late_report_gate.h"

#include <algorithm>
#include <cstdint>

namespace aerie_adblock {

LateReportGate::LateReportGate(base::TimeDelta min_interval, size_t max_reports)
    : min_interval_(min_interval), max_reports_(max_reports) {}
LateReportGate::~LateReportGate() = default;

bool LateReportGate::Allow(std::string_view classes,
                           std::string_view ids,
                           base::TimeTicks now) {
  if ((classes.empty() && ids.empty()) ||
      classes.size() > kMaxLateTokenListBytes ||
      ids.size() > kMaxLateTokenListBytes || sent_ >= max_reports_) {
    return false;
  }
  if (last_ && now - *last_ < min_interval_) {
    return false;
  }
  last_ = now;
  ++sent_;
  return true;
}

void LateReportGate::Reset() {
  sent_ = 0;
  last_.reset();
}

LateTokenBucket::LateTokenBucket(size_t capacity,
                                 base::TimeDelta refill_interval)
    : capacity_(capacity),
      refill_interval_(refill_interval),
      tokens_(capacity) {}
LateTokenBucket::~LateTokenBucket() = default;

void LateTokenBucket::Refill(base::TimeTicks now) {
  if (!last_refill_) {
    last_refill_ = now;
    return;
  }
  if (now <= *last_refill_) {
    return;
  }
  const int64_t gained = (now - *last_refill_) / refill_interval_;
  if (gained <= 0) {
    return;
  }
  tokens_ = std::min<size_t>(capacity_, tokens_ + static_cast<size_t>(gained));
  // Keep the part of an interval that has not yet made a token.
  *last_refill_ += gained * refill_interval_;
}

bool LateTokenBucket::HasToken(base::TimeTicks now) {
  Refill(now);
  return tokens_ > 0;
}

void LateTokenBucket::Take(base::TimeTicks now) {
  Refill(now);
  if (tokens_ > 0) {
    --tokens_;
  }
}

}  // namespace aerie_adblock
