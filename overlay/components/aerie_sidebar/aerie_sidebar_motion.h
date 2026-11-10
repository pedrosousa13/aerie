// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_SIDEBAR_AERIE_SIDEBAR_MOTION_H_
#define COMPONENTS_AERIE_SIDEBAR_AERIE_SIDEBAR_MOTION_H_

#include "components/aerie_design_tokens/aerie_tokens.h"

// What the sidebar animates and with which tokens (issue #177, docs/sidebar-design.md,
// "Motion"). Plain data, so a unit test can check it; the view turns it into
// aerie::motion::AnimateLayer calls. The only properties named here are
// transform (a translation or a scale) and opacity, plus the row height of
// the Today list, which is the one layout exception of the design.
namespace aerie_sidebar {

// Ctrl+S: the sidebar layer slides out of or into view.
struct SlideMotion {
  aerie::tokens::MotionDuration duration;
  aerie::tokens::MotionEasing easing;
  // Normal motion: the layer slides between 0 and `hidden_x` (a translation in
  // px; the sign points to the outer edge). Reduced motion: no transform at all.
  bool animate_transform;
  int hidden_x;
  // Reduced motion: the layer fades instead. Normal motion leaves opacity alone.
  bool animate_opacity;
  float from_opacity;
  float to_opacity;
  // The length of the animation in ms, reduced value included.
  int duration_ms;
};

// `outer_edge_sign` is -1 for a sidebar on the left, +1 for one on the right
// (a right-to-left window flips it too).
SlideMotion ShowMotion(int width, int outer_edge_sign, bool reduced);
SlideMotion HideMotion(int width, int outer_edge_sign, bool reduced);

// A row opens or closes in the Today list.
struct RowMotion {
  // The content layer of the row.
  aerie::tokens::MotionDuration content_duration;
  aerie::tokens::MotionEasing content_easing;
  int content_duration_ms;
  // Open: the content scales from `from_scale` to 1. Close: it slides
  // `slide_x` px (0 under reduced motion, which never moves a layer).
  bool animate_scale;
  float from_scale;
  bool animate_slide;
  int slide_x;
  float from_opacity;
  float to_opacity;
  // The row height. Open: grows from 0, with the content. Close: collapses
  // after the content has faded. Reduced motion: it snaps (no animation).
  bool animate_height;
  aerie::tokens::MotionDuration height_duration;
  aerie::tokens::MotionEasing height_easing;
  int height_duration_ms;
};

RowMotion OpenRowMotion(bool reduced);
RowMotion CloseRowMotion(int outer_edge_sign, bool reduced);

}  // namespace aerie_sidebar

#endif  // COMPONENTS_AERIE_SIDEBAR_AERIE_SIDEBAR_MOTION_H_
