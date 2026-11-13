// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_DRAG_CONTROLLER_H_
#define CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_DRAG_CONTROLLER_H_

#include <memory>
#include <map>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/timer/timer.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_drag_overlay.h"
#include "components/aerie_design_tokens/aerie_motion.h"
#include "components/aerie_sidebar/aerie_sidebar_drop_target.h"
#include "components/aerie_sidebar/aerie_sidebar_move.h"
#include "components/aerie_sidebar/aerie_sidebar_palette.h"
#include "components/aerie_tab_model/folder_tree.h"
#include "ui/base/cursor/cursor.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"

class PrefService;

namespace aerie_tab_model {
class TabModel;
}  // namespace aerie_tab_model

namespace views {
class View;
}  // namespace views

// Dragging a tab or a folder between Today, Pinned, folders and Favorites
// (issue #182, docs/sidebar-design.md, "Drag rules over a folder row" and
// "Motion"). It does not use the tab strip's drag controller and no OS drag and
// drop: the dragged row or tile reports the pointer (it holds the mouse
// capture), this class turns the pointer into a drop target
// (aerie_sidebar::ComputeDropTarget), shows it, and on release asks the host to
// write the move.
//
// The dragged item is drawn again in a drag layer (AerieSidebarDragGhost) above
// the lists, so that no scroll view clips it. The real row keeps its slot and
// is not drawn. Rows that the item passes shift one row with a layer
// transform; nothing lays out while the pointer moves. The controller keeps
// record ids, never row or tile pointers: every time it needs a view it asks
// the host, which may answer null (a row that was removed).
//
// The sidebar creates it on the first drag.
class AerieSidebarDragController {
 public:
  // A row of Pinned or Today as laid out, in the sidebar's coordinates.
  struct MeasuredRow {
    aerie_tab_model::TreeRow tree;
    gfx::Rect rect;
  };
  struct MeasuredLayout {
    std::vector<MeasuredRow> pinned;
    std::vector<MeasuredRow> today;
    // The visible part of each list's scroll view; empty when it is not shown.
    gfx::Rect pinned_viewport;
    gfx::Rect today_viewport;
    // The Favorites grid, and its tiles in order (id and rect).
    gfx::Rect favorites_area;
    std::vector<std::pair<std::string, gfx::Rect>> tiles;
    // The two dividers, with their hit areas.
    gfx::Rect favorites_divider;
    gfx::Rect pinned_divider;
  };

  class Host {
   public:
    virtual ~Host() = default;
    // The view the drag layer and the mark are added to; the coordinates of
    // everything here are its own.
    virtual views::View* SidebarView() = 0;
    virtual aerie_tab_model::TabModel* Model() = 0;
    virtual const PrefService* Prefs() = 0;
    virtual const aerie_sidebar::SidebarPalette& Palette() const = 0;
    virtual bool Dark() const = 0;
    // False while a folder opens or shuts, or rows open or close: a drag does
    // not start then.
    virtual bool CanBeginDrag() = 0;
    // The rows and tiles as laid out now. Nothing in the result outlives the
    // call.
    virtual MeasuredLayout Measure() = 0;
    // The laid-out rect of the row or tile `id`; false when it has none.
    virtual bool RectFor(const std::string& id, gfx::Rect* rect) = 0;
    virtual bool DragContentFor(const std::string& id,
                                AerieSidebarDragContent* content) = 0;
    virtual void SetDragHidden(const std::string& id, bool hidden) = 0;
    virtual void SetDragCursor(const std::string& id,
                               const ui::Cursor& cursor) = 0;
    // Moves the row `id` by `dy` px with the shift motion; 0 puts it back.
    virtual void ShiftRow(const std::string& id, int dy) = 0;
    virtual void ResetRowShift(const std::string& id) = 0;
    // Opens room in an empty Pinned list and an empty Favorites grid so that
    // they can take the drop, and puts them away again.
    // Scrolls the list of `section` by `dy` px; false when it cannot.
    virtual bool ScrollList(aerie_tab_model::Section section, int dy) = 0;
    // Writes the move into the Tab Model and brings the rows and tiles up to
    // date at once (sync and layout), without any FLIP motion.
    virtual aerie_sidebar::MoveResult ApplyMove(
        const aerie_sidebar::MoveRequest& request) = 0;
    // A refused drop. `on_drop`: the user released over it; the hint of the
    // depth limit also shows while the pointer is over it, the Favorites
    // message only at the drop.
    virtual void ShowRefusal(aerie_sidebar::RefusalReason reason,
                             bool on_drop) = 0;
    virtual void HideRefusal() = 0;
    virtual void MoveToNewWindow(const std::string& tab_id) = 0;
  };

  explicit AerieSidebarDragController(Host* host);
  AerieSidebarDragController(const AerieSidebarDragController&) = delete;
  AerieSidebarDragController& operator=(const AerieSidebarDragController&) =
      delete;
  ~AerieSidebarDragController();

  // The pointer crossed the threshold on the row or tile `id`. False: the drag
  // does not start. `is_folder` is a folder row.
  bool Begin(const std::string& id, bool is_folder);
  // The pointer is at `widget_point` (the widget's coordinates).
  void Update(const gfx::Point& widget_point);
  // The release (or Esc / capture loss when `cancelled`). `outside`: the
  // pointer was outside the window.
  void End(const gfx::Point& widget_point, bool cancelled, bool outside);
  // The lists changed under the drag, a window closes, the sidebar hides: end
  // the drag and put everything back at once, without motion.
  void Cancel();
  // Ends a settle that runs (the lists changed, or a new drag starts).
  void FinishSettle();

  bool active() const { return active_; }
  bool settling() const { return settling_; }
  const std::string& dragged_id() const { return id_; }
  const aerie_sidebar::DropTarget& target() const { return target_; }
  // For tests.
  AerieSidebarDragGhost* ghost() const { return ghost_; }
  AerieSidebarDropIndicator* indicator() const { return indicator_; }
  const std::vector<std::string>& hidden_ids() const { return block_ids_; }

 private:
  void EnsureOverlay();
  void RemoveGhost();
  void Reevaluate();
  void HideRefusalIfShown();
  // The shift of each row for the current target, and the index in the target
  // list where the item would be inserted (-1: none).
  std::map<std::string, int> ComputeShifts(const MeasuredLayout& layout,
                                           int* insertion_index) const;
  void ShowFeedback(const MeasuredLayout& layout);
  void ClearFeedback();
  void ApplyShifts(const MeasuredLayout& layout);
  void ResetShifts();
  void UpdateAutoScroll(const gfx::Point& sidebar_point,
                        const MeasuredLayout& layout);
  void TickAutoScroll();
  void PlaceGhost(const gfx::Point& sidebar_point);
  void Unhide();
  // Settles the ghost from where it is to `dest` on the spring, then removes
  // it; the item's real view shows at the end. Instant under reduced motion.
  void SettleTo(const gfx::Rect& dest);
  gfx::Transform LiftTransform(float scale) const;
  aerie_sidebar::DropGeometry BuildGeometry(const MeasuredLayout& layout) const;

  const raw_ptr<Host> host_;
  raw_ptr<AerieSidebarDragGhost> ghost_ = nullptr;
  raw_ptr<AerieSidebarDropIndicator> indicator_ = nullptr;

  bool active_ = false;
  bool settling_ = false;
  std::string id_;
  bool is_folder_ = false;
  aerie_tab_model::Section source_section_ = aerie_tab_model::Section::kToday;
  bool source_is_tile_ = false;
  // The item's laid-out rect when the drag began, and the offset of the
  // pointer inside it.
  gfx::Rect source_rect_;
  gfx::Vector2d grab_;
  // The item and the rows below it that are not drawn while it is dragged.
  std::vector<std::string> block_ids_;
  // The height one row (or the folder with its rows) takes, spacing included.
  int block_height_ = 0;
  gfx::Point last_point_;
  aerie_sidebar::DropTarget target_;
  bool target_valid_ = false;
  bool refusal_shown_ = false;
  // The rows that have a shift now, by id.
  std::map<std::string, int> shift_by_id_;
  bool grab_set_ = false;
  // A drag layer stays this long after the motion's own duration so that the
  // last frame is drawn. Not a design value.
  static constexpr int kSettleSlackMs = 50;
  base::RepeatingTimer scroll_timer_;
  int scroll_dy_ = 0;
  aerie_tab_model::Section scroll_section_ = aerie_tab_model::Section::kToday;
  aerie::motion::MotionHandle ghost_motion_;
  base::OneShotTimer settle_timer_;
  std::vector<std::string> settle_ids_;
};

#endif  // CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_DRAG_CONTROLLER_H_
