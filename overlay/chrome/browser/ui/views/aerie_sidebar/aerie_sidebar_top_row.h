// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_TOP_ROW_H_
#define CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_TOP_ROW_H_

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "components/aerie_sidebar/aerie_sidebar_palette.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/accessible_pane_view.h"

namespace views {
class BoxLayout;
class ImageButton;
}  // namespace views

// The top row of Aerie's sidebar (issue #215, docs/sidebar-design.md,
// "Sidebar regions"): window controls, the sidebar toggle, then Back, Forward
// and Reload. The empty part of the row is the window's drag handle.
//
// The row does not make Back, Forward and Reload: the toolbar's own buttons
// move in (Adopt*), so their commands, enabled state and menus keep working.
// The window controls are the platform's where it draws them (a leading
// inset keeps room for them); where the frame draws them on the far side of
// the window the row draws its own three (ShowWindowControls).
class AerieSidebarTopRow : public views::AccessiblePaneView {
  METADATA_HEADER(AerieSidebarTopRow, views::AccessiblePaneView)

 public:
  struct Callbacks {
    base::RepeatingClosure toggle_sidebar;
    base::RepeatingClosure close_window;
    base::RepeatingClosure minimize_window;
    base::RepeatingClosure toggle_maximize_window;
  };

  AerieSidebarTopRow(Callbacks callbacks,
                     const aerie_sidebar::SidebarPalette& palette);
  AerieSidebarTopRow(const AerieSidebarTopRow&) = delete;
  AerieSidebarTopRow& operator=(const AerieSidebarTopRow&) = delete;
  ~AerieSidebarTopRow() override;

  // The colours of the row's own views (the window controls and the toggle)
  // and their focus rings. Back, Forward and Reload are the toolbar's own
  // buttons and keep drawing from the browser's colour provider.
  void SetPalette(const aerie_sidebar::SidebarPalette& palette);
  const aerie_sidebar::SidebarPalette& palette() const { return palette_; }

  // Room, in pixels, at the leading edge for controls the platform draws over
  // the row (the traffic lights).
  void SetLeadingInset(int inset);
  // Shows the row's own close, minimize and maximize circles.
  void ShowWindowControls(bool show);
  bool window_controls_shown() const;

  // Puts a toolbar button into the row, after the toggle, in the order they
  // are given: Back, Forward, Reload. Null is ignored.
  void AdoptNavigationButton(views::View* button);

  views::View* toggle_button() const;
  views::View* window_controls() const { return window_controls_; }
  views::View* navigation() const { return navigation_; }
  // The rail stacks the controls in a column (issue #184).
  void SetVertical(bool vertical);
  bool vertical() const { return vertical_; }

  // True when `point` (in this view's coordinates) lies on an empty part of
  // the row, which the window manager treats as the title bar.
  bool IsPositionInWindowCaption(const gfx::Point& point) const;

 protected:
  // views::AccessiblePaneView: F6 focuses the sidebar toggle, not a window
  // control (those are reached by a screen reader only).
  views::View* GetDefaultFocusableChild() override;

 private:
  void Rebuild();

  raw_ptr<views::BoxLayout> layout_ = nullptr;
  raw_ptr<views::BoxLayout> controls_layout_ = nullptr;
  raw_ptr<views::BoxLayout> navigation_layout_ = nullptr;
  bool vertical_ = false;
  raw_ptr<views::View> window_controls_ = nullptr;
  raw_ptr<views::ImageButton> toggle_ = nullptr;
  raw_ptr<views::View> spacer_ = nullptr;
  raw_ptr<views::View> navigation_ = nullptr;
  int leading_inset_ = 0;
  aerie_sidebar::SidebarPalette palette_;
};

#endif  // CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_TOP_ROW_H_
