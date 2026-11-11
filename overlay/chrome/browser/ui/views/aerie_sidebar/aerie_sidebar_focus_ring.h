// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_FOCUS_RING_H_
#define CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_FOCUS_RING_H_

#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"
#include "ui/views/view_observer.h"

// The focus ring of the sidebar's own views (issue #215, item 7): a ring of
// `size-focus-ring-width` (2 px), `size-focus-ring-offset` (2 px) away from its
// host, in the Workspace accent. views::FocusRing takes only a Chromium colour
// role, and the accent is a Workspace colour that no role holds, so this view
// draws it from the sidebar palette. Like views::FocusRing it is a child of the
// host that sits outside the host's own bounds, shows while the host has keyboard
// focus, and takes no events. Nothing about it animates.
class AerieSidebarFocusRing : public views::View,
                              public views::ViewObserver {
  METADATA_HEADER(AerieSidebarFocusRing, views::View)

 public:
  // Adds a ring to `host`. `corner_radius` is the host's own corner radius, or
  // negative for a circle (half of the shorter side). The ring is owned by the
  // host.
  static AerieSidebarFocusRing* Install(views::View* host, float corner_radius);
  static AerieSidebarFocusRing* Get(views::View* host);

  AerieSidebarFocusRing(const AerieSidebarFocusRing&) = delete;
  AerieSidebarFocusRing& operator=(const AerieSidebarFocusRing&) = delete;
  ~AerieSidebarFocusRing() override;

  void SetColor(SkColor color);
  SkColor color() const { return color_; }
  // The stroke width and the gap to the host, from the tokens.
  float thickness() const;
  float offset() const;
  // True while the ring is drawn.
  bool shown() const { return GetVisible(); }

  // views::View:
  void OnPaint(gfx::Canvas* canvas) override;

  // views::ViewObserver:
  void OnViewFocused(views::View* observed_view) override;
  void OnViewBlurred(views::View* observed_view) override;
  void OnViewBoundsChanged(views::View* observed_view) override;

 private:
  AerieSidebarFocusRing(views::View* host, float corner_radius);
  void Refresh();

  const raw_ptr<views::View> host_;
  const float corner_radius_;
  SkColor color_;
  base::ScopedObservation<views::View, views::ViewObserver> observation_{this};
};

#endif  // CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_FOCUS_RING_H_
