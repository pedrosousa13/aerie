// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_SIDEBAR_AERIE_SIDEBAR_LAYOUT_H_
#define COMPONENTS_AERIE_SIDEBAR_AERIE_SIDEBAR_LAYOUT_H_

class PrefService;

// What the Aerie sidebar decides without a view (issue #177): which layout is
// on, how wide the column may be, and how the keyboard resizes it. Headless, so
// it has unit tests.
namespace aerie_sidebar {

// Helium's own layout prefs, which aerie_sidebar_layout_lock.h locks to the
// sidebar (ADR 0008). The names and values are those of Helium's
// layout/core.patch (HeliumLayoutType::kVertical is 2; prefs::kHeliumLayout and
// prefs::kHeliumZenMode in chrome/common/pref_names.h). A test compares them
// with Helium's patch, so a Helium bump that renames one fails here.
inline constexpr char kHeliumLayoutPref[] = "helium.browser.layout";
inline constexpr char kHeliumZenModePref[] = "helium.browser.zen_mode";
inline constexpr int kHeliumLayoutVertical = 2;

// True when the layout pref asks for the sidebar. Anything but "horizontal" is
// the sidebar, so a damaged value cannot bring a Helium layout back.
bool IsSidebarLayout(const PrefService& prefs);

// Width limits and steps, from the tokens size-sidebar-width, -min and -max.
int ClampWidth(int width);

// The width to use: the stored pref, clamped. A missing or zero value is the
// default width.
int GetWidth(const PrefService& prefs);

// Left and Right on the focused resize separator change the width by this many
// pixels (issue #177).
inline constexpr int kKeyboardResizeStep = 10;

// The width after one key press. `grow` is true for the key that makes the
// sidebar wider (Right for a sidebar on the left; the caller decides which key
// that is, and flips it for a right-aligned or right-to-left window). The
// result is clamped.
int ResizeByKey(int width, bool grow);

// Whether the sidebar is shown (kVisiblePref).
bool IsVisible(const PrefService& prefs);

// Ctrl+S: flips kVisiblePref and returns the new value. The pref is how the
// shown or hidden state is remembered across restarts.
bool ToggleVisible(PrefService* prefs);

// The width after a drag of the inner edge. `delta_x` is the pointer travel in
// px, positive to the right. A sidebar on the right grows when the pointer
// moves left. The result is clamped.
int ResizeByDrag(int start_width, int delta_x, bool sidebar_on_right);

// What the top row does about the platform's window controls (issue #215).
struct WindowControlsPlan {
  // The room the frame's own controls take at the leading edge (the macOS
  // traffic lights), in whole px, rounded up.
  int leading_inset = 0;
  // True when the top row draws the three controls itself, because the frame
  // draws them on the far side of the window and must be made invisible.
  bool draw_own = false;
};

// `leading_width` and `trailing_width` are the widths the frame reserves at the
// window's two ends for its controls. `frame_may_draw_trailing` is true on the
// platforms whose custom frame puts the controls at the trailing end (Linux):
// there a reserved trailing width means the sidebar draws its own.
WindowControlsPlan PlanWindowControls(float leading_width,
                                      float trailing_width,
                                      bool frame_may_draw_trailing);

}  // namespace aerie_sidebar

#endif  // COMPONENTS_AERIE_SIDEBAR_AERIE_SIDEBAR_LAYOUT_H_
