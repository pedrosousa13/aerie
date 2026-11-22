// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_HOVER_CARD_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_HOVER_CARD_VIEW_H_

#include "base/memory/raw_ptr.h"
#include "components/aerie_sidebar/aerie_sidebar_hover_card.h"
#include "components/aerie_sidebar/aerie_sidebar_palette.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/view.h"

class AerieSidebarPeekSurface;

// What the tab preview card draws (issue #270): a snapshot of the page when
// there is one, then the favicon and the title, the host, and the full URL on
// a line of its own. It only draws: AerieSidebarHoverCard says what is on it.
class AerieHoverCardBody : public views::View {
  METADATA_HEADER(AerieHoverCardBody, views::View)

 public:
  explicit AerieHoverCardBody(const aerie_sidebar::SidebarPalette& palette);
  AerieHoverCardBody(const AerieHoverCardBody&) = delete;
  AerieHoverCardBody& operator=(const AerieHoverCardBody&) = delete;
  ~AerieHoverCardBody() override;

  void SetText(const aerie_sidebar::HoverCardText& text,
               const ui::ImageModel& favicon);
  // Set when the snapshot arrives (it is decoded off the main path), or goes.
  // Empty takes the area away. In memory only, never from disk, and never of a
  // private tab (AerieSidebarHoverCard enforces both).
  void SetSnapshot(const gfx::ImageSkia& snapshot);
  bool snapshot_shown() const { return !snapshot_.isNull(); }

  views::Label* title_label() const { return title_; }
  views::Label* host_label() const { return host_; }
  views::Label* url_label() const { return url_; }

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  void Layout(PassKey) override;
  void OnPaint(gfx::Canvas* canvas) override;

 private:
  gfx::Rect SnapshotBounds() const;
  int ContentWidth() const;

  aerie_sidebar::SidebarPalette palette_;
  raw_ptr<views::ImageView> favicon_;
  raw_ptr<views::Label> title_;
  raw_ptr<views::Label> host_;
  raw_ptr<views::Label> url_;
  gfx::ImageSkia snapshot_;
};

// The card as a window's contents: the opaque card with the `shadow-peek`
// shadow (AerieSidebarPeekSurface) under the body, which has a layer of its own
// so that it draws over the surface's layer. It is a popover: its colours are
// the theme's popover surface and text.
class AerieHoverCardView : public views::View {
  METADATA_HEADER(AerieHoverCardView, views::View)

 public:
  explicit AerieHoverCardView(const aerie_sidebar::SidebarPalette& palette);
  AerieHoverCardView(const AerieHoverCardView&) = delete;
  AerieHoverCardView& operator=(const AerieHoverCardView&) = delete;
  ~AerieHoverCardView() override;

  AerieHoverCardBody* body() const { return body_; }

  // The room the shadow takes on each side of the card.
  static int Inset();
  // The size of the card itself, with no shadow room.
  gfx::Size CardSize() const;

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  void Layout(PassKey) override;

 private:
  raw_ptr<AerieSidebarPeekSurface> surface_;
  raw_ptr<AerieHoverCardBody> body_;
};

#endif  // CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_HOVER_CARD_VIEW_H_
