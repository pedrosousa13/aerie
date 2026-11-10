// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_SIDEBAR_AERIE_SIDEBAR_PREFS_H_
#define COMPONENTS_AERIE_SIDEBAR_AERIE_SIDEBAR_PREFS_H_

class PrefRegistrySimple;

// The prefs of the Aerie sidebar (issue #177). The names are also string
// literals in the settings_private allowlist (patches/sidebar/0020) and a test
// compares them.
namespace aerie_sidebar {

// Which tab layout Aerie shows: "sidebar" (the default) or "horizontal" (the
// layout of issue #186, slice J; this slice only knows the name). Only
// "sidebar" is shown today. A stored value that is none of the two reads as
// "sidebar".
inline constexpr char kLayoutPref[] = "aerie.sidebar.layout";
inline constexpr char kLayoutSidebar[] = "sidebar";
inline constexpr char kLayoutHorizontal[] = "horizontal";

// The width of the sidebar in pixels. The default is the token size-sidebar-
// width; a stored value is clamped to size-sidebar-min .. size-sidebar-max
// when it is read.
inline constexpr char kWidthPref[] = "aerie.sidebar.width";

// Whether the sidebar is shown. aerie_sidebar::ToggleVisible flips it (Ctrl+S) and the view reads it at start.
// The view that calls them is not built yet (docs/sidebar-design.md).
inline constexpr char kVisiblePref[] = "aerie.sidebar.visible";

// Called from chrome::RegisterProfilePrefs (patches/sidebar/0020).
void RegisterProfilePrefs(PrefRegistrySimple* registry);

}  // namespace aerie_sidebar

#endif  // COMPONENTS_AERIE_SIDEBAR_AERIE_SIDEBAR_PREFS_H_
