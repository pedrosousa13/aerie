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

// The auto-hide peek (issue #184): the sidebar floats in on `peek-show`
// (250 ms, `spring`) and out on `peek-hide` (150 ms, `accelerate`). The numbers
// are the tokens'. Reduced motion: a fade, 100 ms in and 100 ms out, no
// transform. Only transform and opacity animate.
SlideMotion PeekShowMotion(int width, int outer_edge_sign, bool reduced);
SlideMotion PeekHideMotion(int width, int outer_edge_sign, bool reduced);
// The duration token the peek hide animates with: `peek-hide`, or `peek-show`
// under reduced motion (one 100 ms fade; the `peek-hide` token's reduced value
// is 90 ms).
aerie::tokens::MotionDuration PeekHideDuration(bool reduced);

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

// A toast with actions (Undo, "Don't file this site", issue #139) holds for
// `motion-timing-toast-action` (6 s) instead of `motion-timing-toast`, so there
// is time to read it and to click.
ToastMotion ToastShowMotion(bool reduced, bool has_actions = false);
ToastMotion ToastHideMotion(bool reduced, bool has_actions = false);

// The rename field swaps in for the title, the title goes out, and the pencil
// of a renamed tab appears (issue #145): an opacity fade on `xfast` (90 ms),
// nothing moves. `xfast` is also what reduced motion allows, so there is no
// reduced variant.
struct RenameFadeMotion {
  float from_opacity;
  float to_opacity;
  aerie::tokens::MotionDuration duration;
  aerie::tokens::MotionEasing easing;
  int duration_ms;
};

// The way in (0 to 1) and the way out (1 to 0).
RenameFadeMotion RenameFade();
RenameFadeMotion RenameFadeOut();

// A folder opens or shuts (issue #181, docs/sidebar-design.md, "Folder reveal
// and collapse"). Reveal: the children fade in on `folder-reveal` (200 ms) and
// `in-out`, the first `stagger_cap` of them `stagger_ms` apart, while the
// container height grows over the same time (the layout exception: the height
// of this one clipped list). Collapse: the children fade out together on
// `xfast` (90 ms) with no stagger, then the height shrinks for the rest of
// `folder-collapse` (180 ms). The two phases are in sequence and the time is
// split from the tokens: nothing is added to `folder-collapse`. Reduced motion:
// opacity only, 90 ms, no stagger; the height snaps.
struct FolderMotion {
  aerie::tokens::MotionDuration duration;
  aerie::tokens::MotionEasing easing;
  int duration_ms;
  float from_opacity;
  float to_opacity;
  // The gap between the starts of the first children, and how many children it
  // applies to. Zero for a collapse and under reduced motion.
  int stagger_ms;
  int stagger_cap;
  bool animate_height;
  // When the height starts, counted from the start of the motion (0: with the
  // fade), and how long it takes.
  int height_delay_ms;
  int height_duration_ms;
  // The length of the whole motion, without the stagger.
  int total_ms;
};

FolderMotion FolderRevealMotion(bool reduced);
FolderMotion FolderCollapseMotion(bool reduced);

// The chevron of a folder turns a quarter turn (`motion-ratio-chevron-turn`)
// on `fast` and `standard`: the transform of its layer. Reduced motion: it
// snaps to the new angle (no animation, no transform element).
struct ChevronMotion {
  bool animate;
  // Share of a full turn the chevron has when open.
  float open_turn;
  aerie::tokens::MotionDuration duration;
  aerie::tokens::MotionEasing easing;
  int duration_ms;
};

ChevronMotion FolderChevronMotion(bool reduced);

RowMotion OpenRowMotion(bool reduced);
RowMotion CloseRowMotion(int outer_edge_sign, bool reduced);

// The folder editor sheet opens and closes (issue #139, docs/sidebar-design.md,
// "Folders"). Open: it slides `slide_px` (`motion-distance-sheet-slide`, 10 px)
// from the sidebar edge while it fades in, on `sheet-enter` and `decelerate`.
// Close: it only fades out, on `sheet-exit` and `accelerate`. Reduced motion:
// opacity only, on the reduced durations (90 ms), no slide.
struct SheetMotion {
  bool animate_slide;
  int slide_px;
  float from_opacity;
  float to_opacity;
  aerie::tokens::MotionDuration duration;
  aerie::tokens::MotionEasing easing;
  int duration_ms;
};

SheetMotion SheetEnterMotion(bool reduced);
SheetMotion SheetExitMotion(bool reduced);

// A folder row pulses after a tab was filed into it (issue #139): a wash of the
// accent colour over the row rises to `peak_opacity` on `rise_duration` and
// `rise_easing`, then falls back to 0 for the rest of `motion-duration-autofile-
// pulse` (700 ms; 300 ms under reduced motion) on `standard`. Only opacity
// moves, reduced motion or not; the reduced pulse is shorter.
struct PulseMotion {
  float peak_opacity;
  aerie::tokens::MotionDuration rise_duration;
  aerie::tokens::MotionEasing rise_easing;
  int rise_ms;
  aerie::tokens::MotionEasing fall_easing;
  int fall_ms;
  // rise_ms + fall_ms.
  int total_ms;
};

PulseMotion AutoFilePulseMotion(bool reduced);

// The thumb of a switch moves on `motion-duration-switch` (120 ms; 60 ms
// reduced) and `standard`. The thumb's transform is the only thing that moves.
struct SwitchMotion {
  aerie::tokens::MotionDuration duration;
  aerie::tokens::MotionEasing easing;
  int duration_ms;
};

SwitchMotion SwitchThumbMotion(bool reduced);

}  // namespace aerie_sidebar

#endif  // COMPONENTS_AERIE_SIDEBAR_AERIE_SIDEBAR_MOTION_H_
