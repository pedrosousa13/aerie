// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_PEEK_SURFACE_H_
#define CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_PEEK_SURFACE_H_

#include "components/aerie_sidebar/aerie_sidebar_palette.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"

// The opaque card and the `shadow-peek` of the sidebar while it floats over the
// page (issue #184). It sits under the sidebar's rows, takes no input, and has
// a layer of its own so that its shadow can fall outside the sidebar's bounds.
// The sidebar view sizes it: the sidebar's rect, grown by `ShadowRoom()`.
class AerieSidebarPeekSurface : public views::View {
  METADATA_HEADER(AerieSidebarPeekSurface, views::View)

 public:
  AerieSidebarPeekSurface();
  AerieSidebarPeekSurface(const AerieSidebarPeekSurface&) = delete;
  AerieSidebarPeekSurface& operator=(const AerieSidebarPeekSurface&) = delete;
  ~AerieSidebarPeekSurface() override;

  // How far the shadow reaches past the card on every side: the blur and the
  // offset of the largest layer of `shadow-peek`.
  static int ShadowRoom();

  void SetColours(const aerie_sidebar::SidebarPalette& palette);

  // views::View:
  void OnPaint(gfx::Canvas* canvas) override;

 private:
  aerie_sidebar::SidebarPalette palette_;
};

#endif  // CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_PEEK_SURFACE_H_
