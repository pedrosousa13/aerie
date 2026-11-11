// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_TOOLBAR_HOOK_H_
#define CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_TOOLBAR_HOOK_H_

#include "build/build_config.h"
#include "chrome/browser/ui/view_ids.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_toolbar_parts.h"
#include "components/aerie_sidebar/aerie_sidebar_layout.h"

namespace aerie_sidebar {

// The one call BrowserView makes after the toolbar has made its buttons
// (patches/sidebar/0031). While the layout is the sidebar, it hands the
// toolbar's address pill, Back, Forward and Reload, and the toolbar itself, to
// the sidebar, and decides about the window controls: where the frame draws
// them on the far side of the window (Linux with the custom frame) the sidebar
// draws its own and the frame's become invisible (Helium's
// SetCaptionButtonsOpacity); a leading exclusion (the macOS traffic lights)
// becomes an inset of the top row.
//
// A template on the three frame types so that this header, which lives in the
// sidebar's target, needs none of their headers, and the sidebar's target does
// not depend on the frame's. The types are VerticalTabStripRegionView,
// BrowserFrameView and ToolbarView; the caller has all three complete.
template <typename Region, typename FrameView, typename Toolbar>
void AdoptToolbar(Region* region, FrameView* frame_view, Toolbar* toolbar) {
  if (!region || !region->has_aerie_sidebar()) {
    return;
  }
  const auto params = frame_view->GetBrowserLayoutParams();
#if BUILDFLAG(IS_LINUX)
  constexpr bool kFrameMayDrawTrailing = true;
#else
  constexpr bool kFrameMayDrawTrailing = false;
#endif
  const WindowControlsPlan plan = PlanWindowControls(
      params.leading_exclusion.ContentWithPadding().width(),
      params.trailing_exclusion.content.width(), kFrameMayDrawTrailing);
  region->AdoptAerieToolbar(
      {toolbar, toolbar->location_bar_view(),
       toolbar->GetViewByID(VIEW_ID_BACK_BUTTON),
       toolbar->GetViewByID(VIEW_ID_FORWARD_BUTTON),
       toolbar->GetViewByID(VIEW_ID_RELOAD_BUTTON)},
      plan.leading_inset, plan.draw_own);
  if (plan.draw_own) {
    frame_view->SetCaptionButtonsOpacity(0.0f);
  }
}

}  // namespace aerie_sidebar

#endif  // CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_TOOLBAR_HOOK_H_
