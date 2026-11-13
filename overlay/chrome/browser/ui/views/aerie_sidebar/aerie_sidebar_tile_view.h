// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_TILE_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_TILE_VIEW_H_

#include <optional>
#include <string>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/timer/timer.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_drag_overlay.h"
#include "components/aerie_design_tokens/aerie_motion.h"
#include "components/aerie_sidebar/aerie_sidebar_palette.h"
#include "components/aerie_sidebar/aerie_sidebar_today_list.h"
#include "ui/base/cursor/cursor.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/base/mojom/menu_source_type.mojom-forward.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/vector2d.h"
#include "ui/views/context_menu_controller.h"
#include "ui/views/view.h"

class PrefService;

namespace views {
class ImageView;
}  // namespace views

// One tile of the Favorites grid (issue #180): the favicon on a rounded tile,
// with the moon badge when the tab is asleep. It says "button" to a screen
// reader, named by the tab's title. A click or Enter selects the tab; a middle
// click, Delete and Ctrl+W close it as the pinned-close setting says. The tile
// is one stop of a roving tab stop (the grid has one Tab stop; the arrow keys
// move it in two dimensions, aerie_sidebar::HandleGridKey). It holds no tab:
// the sidebar view tells it what to show.
class AerieSidebarTileView : public views::View,
                             public views::ContextMenuController {
  METADATA_HEADER(AerieSidebarTileView, views::View)

 public:
  struct Callbacks {
    base::RepeatingClosure select;
    base::RepeatingClosure close;
    base::RepeatingCallback<void(aerie_sidebar::GridKey)> key;
    base::RepeatingClosure focused;
    base::RepeatingCallback<void(const gfx::Point& screen_point,
                                 ui::mojom::MenuSourceType source)>
        context_menu;
    // A double-click or F2 on the tile: the sidebar opens the rename popover
    // (issue #145; a tile has no title to edit in place).
    base::RepeatingClosure begin_rename;
    // Dragging the tile (issue #182); as for AerieSidebarRowView::Callbacks.
    base::RepeatingCallback<bool()> drag_begin;
    base::RepeatingCallback<void(const gfx::Point& widget_point)> drag_update;
    base::RepeatingCallback<void(const gfx::Point& widget_point,
                                 bool cancelled,
                                 bool outside)>
        drag_end;
  };

  AerieSidebarTileView(Callbacks callbacks,
                       const aerie_sidebar::SidebarPalette& palette);
  AerieSidebarTileView(const AerieSidebarTileView&) = delete;
  AerieSidebarTileView& operator=(const AerieSidebarTileView&) = delete;
  ~AerieSidebarTileView() override;

  void SetPalette(const aerie_sidebar::SidebarPalette& palette);
  void SetTitle(const std::u16string& title);
  void SetFavicon(const ui::ImageModel& favicon);
  void SetSelected(bool selected);
  bool selected() const { return selected_; }

  // The same look as a sleeping row (issue #179): the favicon at
  // `motion-opacity-sleeping`, then a static desaturate, and the moon badge. The
  // tooltip says "Asleep, frees memory" and the accessible name ends with
  // ", asleep". `reduced_motion_prefs` as for AerieSidebarRowView::SetSleeping.
  void SetSleeping(bool sleeping, const PrefService* reduced_motion_prefs);
  bool sleeping() const { return sleeping_; }

  // The tab has a name of its own (issue #145): the accessible name ends with
  // ", renamed" (after ", asleep"), and the tooltip adds `renamed_tooltip`
  // ("Page title: ... Shown: your name." and the order) under the name.
  void SetRenamed(bool renamed, const std::u16string& renamed_tooltip);
  bool renamed() const { return renamed_; }

  void SetTabStop(bool tab_stop);
  bool is_tab_stop() const;

  // The tile moved by `delta` since it was last drawn (pin, unpin and Favorites
  // changes, issue #180): it starts there and goes to its place on the spring.
  // Reduced motion puts it at its place at once.
  void PlayFlipFrom(const gfx::Vector2d& delta, const PrefService* prefs);

  // What the drag layer draws for this tile, and the hidden state while it is
  // dragged (a static opacity of 0).
  AerieSidebarDragContent GetDragContent() const;
  void SetDragHidden(bool hidden);
  bool drag_hidden() const { return drag_hidden_; }
  void SetDragCursor(const ui::Cursor& cursor);
  bool dragging() const { return dragging_; }

  // For tests.
  views::ImageView* favicon_view() const { return favicon_; }
  views::ImageView* sleep_badge() const { return sleep_badge_; }
  const std::u16string& title() const { return title_text_; }

  // views::View:
  void Layout(PassKey) override;
  ui::Cursor GetCursor(const ui::MouseEvent& event) override;
  bool OnMousePressed(const ui::MouseEvent& event) override;
  bool OnMouseDragged(const ui::MouseEvent& event) override;
  void OnMouseCaptureLost() override;
  bool AcceleratorPressed(const ui::Accelerator& accelerator) override;
  void OnMouseReleased(const ui::MouseEvent& event) override;
  void OnMouseEntered(const ui::MouseEvent& event) override;
  void OnMouseExited(const ui::MouseEvent& event) override;
  bool OnKeyPressed(const ui::KeyEvent& event) override;
  bool SkipDefaultKeyEventProcessing(const ui::KeyEvent& event) override;
  std::u16string GetRenderedTooltipText(const gfx::Point& p) const override;
  void OnFocus() override;
  void OnBlur() override;
  void OnThemeChanged() override;
  // views::ContextMenuController:
  void ShowContextMenuForViewImpl(
      views::View* source,
      const gfx::Point& point,
      ui::mojom::MenuSourceType source_type) override;

 private:
  void UpdateState();
  void UpdateName();
  void ApplySleepLook(bool animate, const PrefService* prefs);
  void FinishDrag(const gfx::Point& point, bool cancelled, bool outside);

  Callbacks callbacks_;
  aerie_sidebar::SidebarPalette palette_;
  bool selected_ = false;
  bool hovered_ = false;
  bool pressed_ = false;
  bool sleeping_ = false;
  bool sleep_look_applied_ = false;
  bool renamed_ = false;
  bool suppress_release_ = false;
  bool dragging_ = false;
  bool drag_cancelled_ = false;
  bool drag_hidden_ = false;
  gfx::Point press_point_;
  gfx::Point last_point_;
  std::optional<ui::Cursor> drag_cursor_;
  std::u16string renamed_tooltip_;
  std::u16string title_text_;
  raw_ptr<views::ImageView> favicon_ = nullptr;
  raw_ptr<views::ImageView> sleep_badge_ = nullptr;
  aerie::motion::MotionHandle sleep_motion_;
  aerie::motion::MotionHandle flip_motion_;
  base::OneShotTimer desaturate_timer_;
  base::OneShotTimer flip_timer_;
};

#endif  // CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_TILE_VIEW_H_
