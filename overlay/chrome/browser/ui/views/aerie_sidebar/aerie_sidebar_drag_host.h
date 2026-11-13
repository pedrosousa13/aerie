// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_DRAG_HOST_H_
#define CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_DRAG_HOST_H_

#include "base/memory/raw_ptr.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_drag_controller.h"

class AerieSidebarView;

// What the drag controller asks of the sidebar view (issue #182): the rows and
// tiles as laid out, the views of an id, the Tab Model move and the sync that
// follows it. A friend of AerieSidebarView so that the drag wiring stays out of
// aerie_sidebar_view.cc.
class AerieSidebarDragHost : public AerieSidebarDragController::Host {
 public:
  explicit AerieSidebarDragHost(AerieSidebarView* view);
  ~AerieSidebarDragHost() override;

  // AerieSidebarDragController::Host:
  views::View* SidebarView() override;
  aerie_tab_model::TabModel* Model() override;
  const PrefService* Prefs() override;
  const aerie_sidebar::SidebarPalette& Palette() const override;
  bool Dark() const override;
  bool CanBeginDrag() override;
  AerieSidebarDragController::MeasuredLayout Measure() override;
  bool RectFor(const std::string& id, gfx::Rect* rect) override;
  bool DragContentFor(const std::string& id,
                      AerieSidebarDragContent* content) override;
  void SetDragHidden(const std::string& id, bool hidden) override;
  void SetDragCursor(const std::string& id, const ui::Cursor& cursor) override;
  void ShiftRow(const std::string& id, int dy) override;
  void ResetRowShift(const std::string& id) override;
  void SetDropZonesRevealed(bool revealed) override;
  bool ScrollList(aerie_tab_model::Section section, int dy) override;
  aerie_sidebar::MoveResult ApplyMove(
      const aerie_sidebar::MoveRequest& request) override;
  void ShowRefusal(aerie_sidebar::RefusalReason reason, bool on_drop) override;
  void HideRefusal() override;
  void MoveToNewWindow(const std::string& tab_id) override;

  // Where the floating drop bands are while a drag is on (empty rects when the
  // section is not empty, or no drag), for tests.
  gfx::Rect PinnedZone() const;
  gfx::Rect FavoritesZone() const;

 private:
  const raw_ptr<AerieSidebarView> view_;
  // Floating bands over the top of the Today list while a drag is on and the
  // section is empty (they are children of the sidebar view).
  raw_ptr<views::View> pinned_band_ = nullptr;
  raw_ptr<views::View> favorites_band_ = nullptr;
};

#endif  // CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_DRAG_HOST_H_
