// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_DRAG_OVERLAY_H_
#define CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_DRAG_OVERLAY_H_

#include <string>

#include "base/memory/raw_ptr.h"
#include "components/aerie_sidebar/aerie_sidebar_palette.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/view.h"

namespace views {
class ImageView;
class Label;
}  // namespace views

// What a row or a tile lends to the drag layer: enough to draw it again above
// the lists (issue #182).
struct AerieSidebarDragContent {
  std::u16string title;
  ui::ImageModel icon;
  // A tile shows its icon only, centred.
  bool tile = false;
  // The row's indent in px (rows only).
  int indent = 0;
};

// The drag layer (issue #182, "Lift"): a copy of the dragged row or tile that
// floats above both lists and the Favorites grid, so that no scroll view clips
// it. It paints to a layer. The pointer moves it by its bounds; the lift
// scales its layer and the drop settles its layer (transform only). The shadow
// (`shadow-lift`) is painted with the view and is never animated.
class AerieSidebarDragGhost : public views::View {
  METADATA_HEADER(AerieSidebarDragGhost, views::View)

 public:
  // Room around the content for the shadow, in px.
  static constexpr int kShadowRoom = 24;

  AerieSidebarDragGhost(const AerieSidebarDragContent& content,
                        const aerie_sidebar::SidebarPalette& palette,
                        bool dark);
  AerieSidebarDragGhost(const AerieSidebarDragGhost&) = delete;
  AerieSidebarDragGhost& operator=(const AerieSidebarDragGhost&) = delete;
  ~AerieSidebarDragGhost() override;

  // Puts the drawn row at `rect` (the view is larger by the shadow room).
  void SetContentBounds(const gfx::Rect& rect);
  gfx::Rect content_bounds() const;

  // views::View:
  void OnPaint(gfx::Canvas* canvas) override;
  void Layout(PassKey) override;

 private:
  aerie_sidebar::SidebarPalette palette_;
  bool dark_;
  bool tile_;
  int indent_;
  raw_ptr<views::ImageView> icon_ = nullptr;
  raw_ptr<views::Label> title_ = nullptr;
};

// The mark that shows where a drop would land: a line between rows, an outline
// around a folder, or a red outline when the drop is refused. Not animated.
class AerieSidebarDropIndicator : public views::View {
  METADATA_HEADER(AerieSidebarDropIndicator, views::View)

 public:
  enum class Mode { kLine, kOutline, kRefused };

  AerieSidebarDropIndicator(const aerie_sidebar::SidebarPalette& palette,
                            bool dark);
  AerieSidebarDropIndicator(const AerieSidebarDropIndicator&) = delete;
  AerieSidebarDropIndicator& operator=(const AerieSidebarDropIndicator&) =
      delete;
  ~AerieSidebarDropIndicator() override;

  // Shows the mark. `rect` is in the parent's coordinates.
  void Show(Mode mode, const gfx::Rect& rect);
  void Hide();
  Mode mode() const { return mode_; }
  bool shown() const { return shown_; }

  // views::View:
  void OnPaint(gfx::Canvas* canvas) override;

 private:
  aerie_sidebar::SidebarPalette palette_;
  bool dark_;
  Mode mode_ = Mode::kLine;
  bool shown_ = false;
};

#endif  // CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_DRAG_OVERLAY_H_
