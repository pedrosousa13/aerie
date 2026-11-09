// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_TEXT_METRICS_TEXT_METRICS_NOISE_H_
#define COMPONENTS_AERIE_TEXT_METRICS_TEXT_METRICS_NOISE_H_

// The factor that CanvasRenderingContext2D.measureText scales its metrics by
// while fingerprinting protection is on (issue #118), and the decision whether
// a context is protected. Pure code: no Blink, no base, so it has unit tests.
// Blink hashes the site's text-metrics token to a step and only calls this
// (patches/hardening/0018).
namespace aerie_text_metrics {

// The factor is 1 plus or minus this much (0.00015%, so metrics move by at
// most 0.0003% between two sites).
inline constexpr double kMaxDeviation = 1.5e-6;

// The factor comes from one of this many evenly spaced steps.
inline constexpr int kSteps = 1 << 20;

// The factor for a step in [0, kSteps): 1 - kMaxDeviation for step 0,
// 1 + kMaxDeviation for the last step. A step outside the range is clamped.
double NoiseFactor(int step);

// What decides whether a context gets the factor instead of the real metrics.
struct Policy {
  // The HeliumNoise feature.
  bool master_enabled;
  // The HeliumNoiseTextMetrics feature.
  bool surface_enabled;
  // Whether the browser sent this context a kTextMetrics token (it sends none
  // when protection is off globally or for the site).
  bool has_token;
};

// A context without a token reads the real metrics, so that turning
// protection off, globally or for the site, restores the real values.
bool ShouldNoiseTextMetrics(const Policy& policy);

}  // namespace aerie_text_metrics

#endif  // COMPONENTS_AERIE_TEXT_METRICS_TEXT_METRICS_NOISE_H_
