// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_sidebar/aerie_sidebar_hz_policy.h"

#include <algorithm>

#include "components/aerie_design_tokens/aerie_tokens.h"

namespace aerie_sidebar {

namespace {

// The slide of a closing tab, in px (docs/sidebar-design.md: "slides 8 px
// down"). The design gives it in words only, so it is the one number here.
constexpr int kCloseSlidePx = 8;

}  // namespace

namespace {

std::optional<HzMetrics>& ActiveMetrics() {
  static std::optional<HzMetrics> metrics;
  return metrics;
}

}  // namespace

HzMetrics GetHzMetrics(HzRows rows, HzDensity density) {
  namespace t = aerie::tokens;
  const bool compact = density == HzDensity::kCompact;
  const bool two = rows == HzRows::kTwo;
  HzMetrics m;
  if (two) {
    m.strip_height =
        compact ? t::kSizeHzStripHeightTwoRowCompact : t::kSizeHzStripHeightTwoRow;
    m.tab_height =
        compact ? t::kSizeHzTabHeightTwoRowCompact : t::kSizeHzTabHeightTwoRow;
    m.toolbar_height =
        compact ? t::kSizeHzToolbarHeightCompact : t::kSizeHzToolbarHeight;
    m.tile = compact ? t::kSizeHzTileStripTwoRowCompact : t::kSizeHzTileStrip;
  } else {
    m.strip_height = compact ? t::kSizeHzStripHeightCompact : t::kSizeHzStripHeight;
    m.tab_height = compact ? t::kSizeHzTabHeightCompact : t::kSizeHzTabHeight;
    m.toolbar_height = m.strip_height;
    m.tile = compact ? t::kSizeHzTileStripCompact : t::kSizeHzTileStrip;
  }
  m.tab_strip_padding = m.strip_height - m.tab_height;
  m.tab_max = compact ? t::kSizeHzTabMaxCompact : t::kSizeHzTabMax;
  m.favicon = compact ? t::kSizeHzFaviconCompact : t::kSizeHzFavicon;
  m.address_pill = t::kSizeHzAddressPill;
  m.address_pill_min = t::kSizeHzAddressPillMin;
  m.flare_radius = t::kSizeHzFlareRadius;
  m.scroll_fade = t::kSizeHzScrollFade;
  return m;
}

void SetActiveHzMetrics(std::optional<HzMetrics> metrics) {
  ActiveMetrics() = metrics;
}

std::optional<HzMetrics> GetActiveHzMetrics() {
  return ActiveMetrics();
}

HzTabLayoutResult::HzTabLayoutResult() = default;
HzTabLayoutResult::HzTabLayoutResult(const HzTabLayoutResult&) = default;
HzTabLayoutResult& HzTabLayoutResult::operator=(const HzTabLayoutResult&) =
    default;
HzTabLayoutResult::~HzTabLayoutResult() = default;

HzTabLayoutResult ComputeHzTabLayout(const HzTabLayoutInput& input) {
  HzTabLayoutResult result;
  const int count = std::max(0, input.tab_count);
  if (count == 0) {
    return result;
  }
  const int max_width = input.density == HzDensity::kCompact
                            ? aerie::tokens::kSizeHzTabMaxCompact
                            : aerie::tokens::kSizeHzTabMax;
  const int min_width = aerie::tokens::kSizeHzTabMin;
  const int selected_min = aerie::tokens::kSizeHzTabSelectedMin;
  const int available = std::max(0, input.available_width);
  const bool has_selected = input.selected_index >= 0 &&
                            input.selected_index < count;

  const int ideal = std::clamp(available / count, min_width, max_width);
  int selected_width = 0;
  int other_width = ideal;
  if (has_selected) {
    selected_width = std::max(ideal, selected_min);
    if (count > 1) {
      other_width = std::clamp((available - selected_width) / (count - 1),
                               min_width, max_width);
    }
  }
  result.widths.assign(count, other_width);
  if (has_selected) {
    result.widths[input.selected_index] = selected_width;
  }
  int total = 0;
  for (int width : result.widths) {
    total += width;
  }
  // The widths are clamped at the minimum, so a total above the room means the
  // tabs cannot all be shown.
  result.scroll_needed = total > available;
  result.icon_only.reserve(count);
  for (int i = 0; i < count; ++i) {
    result.icon_only.push_back(ShowsIconOnly(result.widths[i],
                                             has_selected &&
                                                 i == input.selected_index));
  }
  return result;
}

bool ShowsIconOnly(int tab_width, bool selected) {
  return !selected && tab_width < aerie::tokens::kSizeHzTabIconOnly;
}

HzTabMotion PlanHzTabMotion(HzTabEvent event, bool reduced) {
  HzTabMotion motion;
  motion.animate_opacity = true;
  motion.animate_transform = !reduced;
  if (event == HzTabEvent::kOpen) {
    motion.duration_ms = reduced ? aerie::tokens::kMotionDurationTabOpenReduced
                                 : aerie::tokens::kMotionDurationTabOpen;
    motion.width_duration_ms = motion.duration_ms;
    motion.slide_px = 0;
    return motion;
  }
  motion.duration_ms = reduced ? aerie::tokens::kMotionDurationXfastReduced
                               : aerie::tokens::kMotionDurationXfast;
  motion.width_duration_ms = reduced ? aerie::tokens::kMotionDurationTabCloseReduced
                                     : aerie::tokens::kMotionDurationTabClose;
  motion.slide_px = reduced ? 0 : kCloseSlidePx;
  return motion;
}

}  // namespace aerie_sidebar
