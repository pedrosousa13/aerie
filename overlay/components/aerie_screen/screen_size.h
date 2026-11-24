// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_SCREEN_SCREEN_SIZE_H_
#define COMPONENTS_AERIE_SCREEN_SCREEN_SIZE_H_

// What window.screen, the device-width and device-height media features and
// window.screenX/screenY report while fingerprinting protection is on (issue
// #116). Pure code: no Blink, no base, so it has unit tests. Blink only decides
// when to call it (patches/hardening/0015).
namespace aerie_screen {

struct Size {
  int width;
  int height;
  friend bool operator==(const Size&, const Size&) = default;
};

// The screen size reported for a window: the smallest of the buckets below
// (1280x800, 1366x768, 1440x900, 1680x1050, 1920x1080, 2560x1440, 3840x2160)
// that is at least as wide and as tall as the window's outer size. The list
// follows Brave's. A window larger than the last bucket in a dimension keeps
// its own size in that dimension, so the screen is never smaller than the
// window.
Size ScreenSizeForWindow(int outer_width, int outer_height);

struct PointF {
  float x;
  float y;
  friend bool operator==(const PointF&, const PointF&) = default;
};

// The screen position a protected window reports for a mouse, pointer, touch or
// drag event (issue #159). A protected window reports its position as 0,0, so
// the event's screen point is its client point (in CSS pixels, as clientX reads
// it, so the result is the same at every zoom) plus the reported difference between
// the outer and the inner size. Then screenX - clientX equals what outerWidth
// and innerWidth report, and no real window position or screen reaches the
// page through an event.
PointF EventScreenPoint(PointF point_in_page,
                        Size reported_outer,
                        Size reported_inner);

// screen.colorDepth and screen.pixelDepth.
inline constexpr int kColorDepth = 24;

// What decides whether a window reports the bucket instead of the real screen.
struct Policy {
  // The HeliumNoise feature.
  bool master_enabled;
  // The HeliumNoiseScreen feature.
  bool surface_enabled;
  // Whether the browser sent this window a kScreen token (it sends none when
  // protection is off globally or for the site).
  bool has_token;
};

// A window without a token shows the real screen, so
// that turning protection off, globally or for the site, restores real values.
bool ShouldProtectScreen(const Policy& policy);

}  // namespace aerie_screen

#endif  // COMPONENTS_AERIE_SCREEN_SCREEN_SIZE_H_
