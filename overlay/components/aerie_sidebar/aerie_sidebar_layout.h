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

// Helium's own layout prefs. The names and values are those of Helium's
// layout/core.patch (HeliumLayoutType::kVertical is 2; prefs::kHeliumLayout and
// prefs::kHeliumZenMode in chrome/common/pref_names.h). A test compares them
// with Helium's patch, so a Helium bump that renames one fails here.
inline constexpr char kHeliumLayoutPref[] = "helium.browser.layout";
inline constexpr char kHeliumZenModePref[] = "helium.browser.zen_mode";
inline constexpr int kHeliumLayoutVertical = 2;

// True when the layout pref asks for the sidebar. Anything but "horizontal" is
// the sidebar, so a damaged value cannot bring a Helium layout back.
bool IsSidebarLayout(const PrefService& prefs);

// While the layout is the sidebar, puts Helium's layout state where the
// sidebar needs it: layout vertical, Zen mode off. It writes only when a value
// differs. Whoever wrote the other value (Settings, an old profile, sync, an
// extension) loses. Safe to call again.
void EnforceHeliumLayout(PrefService* prefs);

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

}  // namespace aerie_sidebar

#endif  // COMPONENTS_AERIE_SIDEBAR_AERIE_SIDEBAR_LAYOUT_H_
