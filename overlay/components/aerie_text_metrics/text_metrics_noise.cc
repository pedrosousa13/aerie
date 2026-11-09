// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_text_metrics/text_metrics_noise.h"

#include <algorithm>

namespace aerie_text_metrics {

double NoiseFactor(int step) {
  const int clamped = std::clamp(step, 0, kSteps - 1);
  // 0 maps to -1 and the last step to +1.
  const double unit =
      2.0 * static_cast<double>(clamped) / static_cast<double>(kSteps - 1) - 1.0;
  return 1.0 + unit * kMaxDeviation;
}

bool ShouldNoiseTextMetrics(const Policy& policy) {
  return policy.master_enabled && policy.surface_enabled && policy.has_token;
}

}  // namespace aerie_text_metrics
