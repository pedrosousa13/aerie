// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_FRAME_AERIE_FRAME_CORNERS_H_
#define COMPONENTS_AERIE_FRAME_AERIE_FRAME_CORNERS_H_

namespace aerie_frame {

// The radius, in DIPs, of the window's top corners (issue #249).
//
// `provider_radius` is what the OS frame provider reports (the GTK theme's
// window radius). `restored` is false when the window is maximised, fullscreen
// or tiled. `can_draw_transparent_corners` is Chromium's own check for whether
// the window may round at all (a custom frame on a compositor with
// translucency): upstream's answer, not a new one.
//
// Square (0) unless the window is restored and can draw transparent corners.
// Then the provider's radius is used as it is, and when the provider reports
// none (KDE with a theme that has no window radius) the page card's outer
// radius, radius-xl. The design has no separate window-corner token.
float WindowCornerRadius(float provider_radius,
                         bool restored,
                         bool can_draw_transparent_corners);

}  // namespace aerie_frame

#endif  // COMPONENTS_AERIE_FRAME_AERIE_FRAME_CORNERS_H_
