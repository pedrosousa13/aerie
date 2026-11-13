// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_drag_host.h"

#include <algorithm>

#include "base/auto_reset.h"
#include "base/strings/string_number_conversions.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model_delegate.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_row_view.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_tile_view.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_view.h"
#include "chrome/grit/generated_resources.h"
#include "components/aerie_design_tokens/aerie_tokens.h"
#include "components/aerie_tab_model/tab_model.h"
#include "components/prefs/pref_service.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/color/color_provider_key.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/widget/widget.h"

namespace {

gfx::Rect RectIn(views::View* ancestor, views::View* view) {
  return views::View::ConvertRectToTarget(view->parent(), ancestor,
                                          view->bounds());
}

}  // namespace

AerieSidebarDragHost::AerieSidebarDragHost(AerieSidebarView* view)
    : view_(view) {}

AerieSidebarDragHost::~AerieSidebarDragHost() = default;

views::View* AerieSidebarDragHost::SidebarView() {
  return view_;
}

aerie_tab_model::TabModel* AerieSidebarDragHost::Model() {
  return view_->model_;
}

const PrefService* AerieSidebarDragHost::Prefs() {
  return view_->browser_->GetProfile()->GetPrefs();
}

const aerie_sidebar::SidebarPalette& AerieSidebarDragHost::Palette() const {
  return view_->palette_;
}

bool AerieSidebarDragHost::Dark() const {
  return view_->GetWidget() && view_->GetWidget()->GetColorMode() ==
                                   ui::ColorProviderKey::ColorMode::kDark;
}

bool AerieSidebarDragHost::CanBeginDrag() {
  if (!view_->model_ || !view_->closing_rows_.empty() ||
      view_->folders_.collapsing_row_count() > 0 || view_->folder_group()) {
    return false;
  }
  for (const auto* rows : {&view_->rows_, &view_->pinned_rows_}) {
    for (const AerieSidebarView::Row& row : *rows) {
      if (row.view->height_animating() || row.view->is_closing()) {
        return false;
      }
    }
  }
  return true;
}

AerieSidebarDragController::MeasuredLayout AerieSidebarDragHost::Measure() {
  AerieSidebarDragController::MeasuredLayout layout;
  auto rows_of = [&](const std::vector<AerieSidebarView::Row>& rows,
                     const std::vector<aerie_tab_model::TreeRow>& tree,
                     std::vector<AerieSidebarDragController::MeasuredRow>* out) {
    if (rows.size() != tree.size()) {
      return;
    }
    for (size_t i = 0; i < rows.size(); ++i) {
      out->push_back({tree[i], RectIn(view_, rows[i].view)});
    }
  };
  if (view_->scroll_->GetVisible()) {
    layout.today_viewport = RectIn(view_, view_->scroll_);
    rows_of(view_->rows_, view_->today_tree_rows_, &layout.today);
  }
  if (view_->pinned_scroll_->GetVisible()) {
    layout.pinned_viewport = RectIn(view_, view_->pinned_scroll_);
    rows_of(view_->pinned_rows_, view_->pinned_tree_rows_, &layout.pinned);
  }
  layout.favorites_divider = RectIn(view_, view_->favorites_divider_);
  layout.pinned_divider = RectIn(view_, view_->pinned_divider_);
  if (view_->favorites_grid_->GetVisible()) {
    layout.favorites_area = RectIn(view_, view_->favorites_grid_);
    for (const AerieSidebarView::Tile& tile : view_->tiles_) {
      layout.tiles.emplace_back(tile.tab_id, RectIn(view_, tile.view));
    }
  }
  return layout;
}

bool AerieSidebarDragHost::RectFor(const std::string& id, gfx::Rect* rect) {
  if (AerieSidebarRowView* row = view_->RowForId(id)) {
    *rect = RectIn(view_, row);
    return true;
  }
  for (const AerieSidebarView::Tile& tile : view_->tiles_) {
    if (tile.tab_id == id) {
      *rect = RectIn(view_, tile.view);
      return true;
    }
  }
  return false;
}

bool AerieSidebarDragHost::DragContentFor(const std::string& id,
                                          AerieSidebarDragContent* content) {
  if (AerieSidebarRowView* row = view_->RowForId(id)) {
    *content = row->GetDragContent();
    return true;
  }
  for (const AerieSidebarView::Tile& tile : view_->tiles_) {
    if (tile.tab_id == id) {
      *content = tile.view->GetDragContent();
      return true;
    }
  }
  return false;
}

void AerieSidebarDragHost::SetDragHidden(const std::string& id, bool hidden) {
  if (AerieSidebarRowView* row = view_->RowForId(id)) {
    row->SetDragHidden(hidden);
    return;
  }
  for (const AerieSidebarView::Tile& tile : view_->tiles_) {
    if (tile.tab_id == id) {
      tile.view->SetDragHidden(hidden);
      return;
    }
  }
}

void AerieSidebarDragHost::SetDragCursor(const std::string& id,
                                         const ui::Cursor& cursor) {
  if (AerieSidebarRowView* row = view_->RowForId(id)) {
    row->SetDragCursor(cursor);
    return;
  }
  for (const AerieSidebarView::Tile& tile : view_->tiles_) {
    if (tile.tab_id == id) {
      tile.view->SetDragCursor(cursor);
      return;
    }
  }
}

void AerieSidebarDragHost::ShiftRow(const std::string& id, int dy) {
  if (AerieSidebarRowView* row = view_->RowForId(id)) {
    row->ShiftTo(dy, Prefs());
  }
}

void AerieSidebarDragHost::ResetRowShift(const std::string& id) {
  if (AerieSidebarRowView* row = view_->RowForId(id)) {
    if (!row->drag_hidden()) {
      row->ResetDrag();
    }
  }
}

bool AerieSidebarDragHost::ScrollList(aerie_tab_model::Section section,
                                      int dy) {
  views::ScrollView* scroll = section == aerie_tab_model::Section::kPinned
                                  ? view_->pinned_scroll_.get()
                                  : view_->scroll_.get();
  views::View* list = scroll->contents();
  // The part of the list that shows is the scroll view's height, from the
  // negative of the list's offset.
  const gfx::Rect before(0, -list->y(), list->width(), scroll->height());
  gfx::Rect wanted = before;
  wanted.Offset(0, dy);
  list->ScrollRectToVisible(wanted);
  return list->y() != -before.y();
}

aerie_sidebar::MoveResult AerieSidebarDragHost::ApplyMove(
    const aerie_sidebar::MoveRequest& request) {
  if (!view_->model_) {
    return aerie_sidebar::MoveResult::kInvalid;
  }
  const std::map<std::string, tabs::TabInterface*> lookup =
      view_->BuildTabLookup();
  auto listed = [&](const std::string& id) {
    return lookup.find(id) != lookup.end();
  };
  auto url_for = [&](const std::string& id) {
    auto it = lookup.find(id);
    if (it == lookup.end()) {
      return std::string();
    }
    tabs::TabInterface* tab = it->second;
    GURL url;
    if (content::WebContents* contents = tab->GetContents()) {
      url = contents->GetLastCommittedURL();
    }
    if (url.is_empty()) {
      url = tab->GetURL();
    }
    return url.spec();
  };
  const aerie_sidebar::MoveResult result =
      aerie_sidebar::ApplyMove(*view_->model_, request, listed, url_for);
  if (result == aerie_sidebar::MoveResult::kMoved) {
    // The lists follow at once, so that the drag layer can land on the new
    // place. No FLIP: the layer is the motion.
    view_->SyncAll(/*allow_flip=*/false);
    view_->DeprecatedLayoutImmediately();
  }
  return result;
}

void AerieSidebarDragHost::ShowRefusal(aerie_sidebar::RefusalReason reason,
                                       bool on_drop) {
  switch (reason) {
    case aerie_sidebar::RefusalReason::kTooDeep:
      view_->ShowToast(l10n_util::GetStringFUTF16(
          IDS_AERIE_SIDEBAR_FOLDER_TOO_DEEP,
          base::NumberToString16(aerie_tab_model::kMaxFolderDepth)));
      break;
    case aerie_sidebar::RefusalReason::kFavoritesFull:
      if (on_drop) {
        view_->ShowToast(l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_FAVORITES_FULL));
      }
      break;
    default:
      break;
  }
}

void AerieSidebarDragHost::HideRefusal() {
  view_->HideToast();
}

void AerieSidebarDragHost::MoveToNewWindow(const std::string& tab_id) {
  TabStripModel* model = view_->browser_->GetTabStripModel();
  tabs::TabInterface* tab = view_->FindTab(tab_id);
  TabStripModelDelegate* delegate = model->delegate();
  if (tab && delegate) {
    const std::vector<int> indices = {model->GetIndexOfTab(tab)};
    if (delegate->CanMoveTabsToWindow(indices)) {
      delegate->MoveTabsToNewWindow(indices);
    }
  }
}
