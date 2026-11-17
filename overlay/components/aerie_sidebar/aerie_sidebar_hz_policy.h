// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_SIDEBAR_AERIE_SIDEBAR_HZ_POLICY_H_
#define COMPONENTS_AERIE_SIDEBAR_AERIE_SIDEBAR_HZ_POLICY_H_

#include <vector>

// The pure rules of the horizontal tab layouts (issue #186, docs/sidebar-design.md,
// "Horizontal tabs"). Every size and duration is a token (size-hz-*,
// motion-duration-tab-open and tab-close); nothing is written here twice.
namespace aerie_sidebar {

enum class HzDensity { kComfortable, kCompact };

struct HzTabLayoutInput {
  // The room for tabs, in px.
  int available_width = 0;
  int tab_count = 0;
  // The selected tab, or -1 for none.
  int selected_index = -1;
  HzDensity density = HzDensity::kComfortable;
};

struct HzTabLayoutResult {
  HzTabLayoutResult();
  HzTabLayoutResult(const HzTabLayoutResult&);
  HzTabLayoutResult& operator=(const HzTabLayoutResult&);
  ~HzTabLayoutResult();

  // One per tab, in px.
  std::vector<int> widths;
  // One per tab: the tab is narrower than size-hz-tab-icon-only and shows its
  // favicon only.
  std::vector<bool> icon_only;
  // True when even the narrowest tabs do not fit, so the group scrolls with
  // fading edges and chevron buttons.
  bool scroll_needed = false;
};

// Tabs share the room. They stay at the density's maximum (size-hz-tab-max, or
// its compact value), shrink toward size-hz-tab-min, and the selected tab never
// goes below size-hz-tab-selected-min (it takes the room, and the others share
// the rest). A tab narrower than size-hz-tab-icon-only is icon-only. If the tabs
// at their narrowest do not fit, `scroll_needed` is set and they stay at that
// width.
HzTabLayoutResult ComputeHzTabLayout(const HzTabLayoutInput& input);

// Whether a tab of this width (without the flare on each side) shows its
// favicon only. The selected tab never does.
bool ShowsIconOnly(int tab_width, bool selected);

enum class HzTabEvent { kOpen, kClose };

// What a tab does when it opens or closes.
struct HzTabMotion {
  // How long the tab's own transform and opacity take, in ms.
  int duration_ms = 0;
  // How long the width of the strip's tabs takes (Chromium's own animation).
  int width_duration_ms = 0;
  // Reduced motion animates no transform.
  bool animate_transform = false;
  bool animate_opacity = false;
  // A closing tab slides this far down. 0 under reduced motion and for an
  // opening tab.
  int slide_px = 0;
};

// `reduced` is aerie::motion::ShouldReduceMotion: the operating system's flag or
// the in-app one. Open: scale in and fade over tab-open (reduced: fade, 140 ms).
// Close: slide 8 px down and fade over xfast, then the width collapses over
// tab-close (reduced: fade, 90 ms).
HzTabMotion PlanHzTabMotion(HzTabEvent event, bool reduced);

}  // namespace aerie_sidebar

#endif  // COMPONENTS_AERIE_SIDEBAR_AERIE_SIDEBAR_HZ_POLICY_H_
