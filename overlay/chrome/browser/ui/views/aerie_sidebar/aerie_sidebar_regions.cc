// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_regions.h"

#include <algorithm>
#include <utility>

#include "chrome/browser/ui/view_ids.h"
#include "components/aerie_design_tokens/aerie_tokens.h"
#include "ui/base/metadata/metadata_impl_macros.h"

AerieSidebarPill::AerieSidebarPill() = default;
AerieSidebarPill::~AerieSidebarPill() = default;

void AerieSidebarPill::RequestFocus() {
  if (views::View* omnibox = GetViewByID(VIEW_ID_OMNIBOX)) {
    omnibox->RequestFocus();
  }
}

AerieSidebarTodayTree::AerieSidebarTodayTree() = default;
AerieSidebarTodayTree::~AerieSidebarTodayTree() = default;

void AerieSidebarTodayTree::SetFocusHandler(
    base::RepeatingClosure focus_tab_stop) {
  focus_tab_stop_ = std::move(focus_tab_stop);
}

void AerieSidebarTodayTree::RequestFocus() {
  if (focus_tab_stop_) {
    focus_tab_stop_.Run();
  }
}

AerieSidebarFavoritesGrid::AerieSidebarFavoritesGrid() = default;
AerieSidebarFavoritesGrid::~AerieSidebarFavoritesGrid() = default;

void AerieSidebarFavoritesGrid::SetFocusHandler(
    base::RepeatingClosure focus_tab_stop) {
  focus_tab_stop_ = std::move(focus_tab_stop);
}

int AerieSidebarFavoritesGrid::columns() const {
  return aerie::tokens::kSizeFavoritesColumns;
}

void AerieSidebarFavoritesGrid::RequestFocus() {
  if (focus_tab_stop_) {
    focus_tab_stop_.Run();
  }
}

gfx::Size AerieSidebarFavoritesGrid::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  const int count = static_cast<int>(children().size());
  const int rows = (count + columns() - 1) / columns();
  const int height =
      rows == 0 ? 0
                : rows * aerie::tokens::kSizeTileComfortable +
                      (rows - 1) * aerie::tokens::kSpace8;
  return gfx::Size(available_size.width().value_or(aerie::tokens::kSizeSidebarMin),
                   height);
}

void AerieSidebarFavoritesGrid::Layout(PassKey) {
  const int gap = aerie::tokens::kSpace8;
  const int cols = columns();
  // Each cell is as wide as the room allows; the odd pixels go to the first
  // cells of a row so that the row fills the width.
  const int room = std::max(0, width() - gap * (cols - 1));
  const int base_width = room / cols;
  const int extra = room % cols;
  int index = 0;
  for (views::View* tile : children()) {
    const int row = index / cols;
    const int col = index % cols;
    int x = col * (base_width + gap) + std::min(col, extra);
    const int w = base_width + (col < extra ? 1 : 0);
    tile->SetBounds(x,
                    row * (aerie::tokens::kSizeTileComfortable + gap), w,
                    aerie::tokens::kSizeTileComfortable);
    ++index;
  }
}

BEGIN_METADATA(AerieSidebarPill)
END_METADATA

BEGIN_METADATA(AerieSidebarTodayTree)
END_METADATA

BEGIN_METADATA(AerieSidebarFavoritesGrid)
END_METADATA
