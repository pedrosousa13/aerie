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

// While a hide slides out the window layout already gives the room to the page,
// in one step: the column takes no room (preferred and minimum width 0) and its
// content keeps the width it had, hanging outside the region's own bounds
// toward the page, so it slides out over the room the page is taking
// (issue #215).
class HideSlide {
 public:
  void Begin(int column_width) {
    width_ = column_width;
    active_ = true;
  }
  void End() { active_ = false; }
  bool active() const { return active_; }

  // The content's x and width in the region's coordinates. A column on the
  // right hangs out to the left of its zero-width region.
  int content_x(bool right_aligned) const { return right_aligned ? -width_ : 0; }
  int content_width() const { return width_; }

 private:
  bool active_ = false;
  int width_ = 0;
};

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

// Dragging a Today row to reorder it (docs/sidebar-design.md, "Motion", row
// "Drag"). Lift: the row scales to `lift_scale` on `lift_duration`. Shift: the
// rows it passes move one slot on `shift_duration`. Settle: the dropped row
// springs from where it was released. The dragged row always follows the
// pointer, reduced motion or not; it is then a plain property set, not an
// animation. Reduced motion: no lift, no shift, an instant drop.
struct DragMotion {
  bool animate_lift;
  float lift_scale;
  aerie::tokens::MotionDuration lift_duration;
  aerie::tokens::MotionEasing lift_easing;
  bool animate_shift;
  aerie::tokens::MotionDuration shift_duration;
  aerie::tokens::MotionEasing shift_easing;
  bool animate_settle;
  // The spring duration, `motion-spring-duration`: 420 ms, and its reduced
  // value 140 ms. It is not a `motion-duration` token, so it is given in ms.
  int settle_duration_ms;
  int settle_reduced_duration_ms;
  aerie::tokens::MotionEasing settle_easing;
};

DragMotion RowDragMotion(bool reduced);

// A tab goes to sleep (issue #179): the favicon fades to
// `motion-opacity-sleeping` on `fast` and `standard`, and only opacity moves.
// Then a static desaturate applies; it is never animated. Reduced motion: no
// animation, the favicon takes both looks at once.
struct SleepMotion {
  bool animate_fade;
  aerie::tokens::MotionDuration duration;
  aerie::tokens::MotionEasing easing;
  float asleep_opacity;
};

SleepMotion SleepFaviconMotion(bool reduced);

// Pin and unpin (issue #180, docs/sidebar-design.md, "Motion"): FLIP. The tab
// that changed section moves from its old place to its new one on the spring
// (`motion-spring-duration`, 420 ms; `spring`), and the rows around it close
// the gap on `base` and `standard`. All of it is the transform of a layer.
// Reduced motion: nothing moves; each row is at its new place at once.
struct PinMotion {
  bool animate;
  // The tab that moved.
  int moved_duration_ms;
  int moved_reduced_duration_ms;
  aerie::tokens::MotionEasing moved_easing;
  // The other rows.
  aerie::tokens::MotionDuration others_duration;
  aerie::tokens::MotionEasing others_easing;
};

PinMotion PinFlipMotion(bool reduced);

// The context menu opens (issue #180): it scales from `from_scale` (0.97) to 1
// and fades in on `fast` and `decelerate`. There is no closing animation.
// Reduced motion: opacity only, on the reduced `fast` duration (90 ms).
struct MenuOpenMotion {
  bool animate_scale;
  float from_scale;
  float from_opacity;
  aerie::tokens::MotionDuration duration;
  aerie::tokens::MotionEasing easing;
  int duration_ms;
};

MenuOpenMotion ContextMenuOpenMotion(bool reduced);

// The toast (issue #180; docs/sidebar-design.md, "Motion"): it rises
// `rise_px` and fades in on `fast` and `decelerate`, holds for `hold_ms`
// (`motion-timing-toast`, a toast without actions), then fades out on `fast` and `accelerate`.
// Reduced motion: opacity only, no rise.
struct ToastMotion {
  bool animate_rise;
  int rise_px;
  float from_opacity;
  float to_opacity;
  aerie::tokens::MotionDuration duration;
  aerie::tokens::MotionEasing easing;
  int duration_ms;
  int hold_ms;
};

ToastMotion ToastShowMotion(bool reduced);
ToastMotion ToastHideMotion(bool reduced);

// The rename field swaps in for the title, and the pencil of a renamed tab
// appears (issue #145): an opacity fade on `xfast` (90 ms), nothing moves. It
// is already the reduced-motion version, so the two cases are the same.
struct RenameFadeMotion {
  float from_opacity;
  float to_opacity;
  aerie::tokens::MotionDuration duration;
  aerie::tokens::MotionEasing easing;
  int duration_ms;
};

RenameFadeMotion RenameFade(bool reduced);

RowMotion OpenRowMotion(bool reduced);
RowMotion CloseRowMotion(int outer_edge_sign, bool reduced);

}  // namespace aerie_sidebar

#endif  // COMPONENTS_AERIE_SIDEBAR_AERIE_SIDEBAR_MOTION_H_
