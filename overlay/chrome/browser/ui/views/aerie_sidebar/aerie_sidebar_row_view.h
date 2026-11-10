// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_ROW_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_ROW_VIEW_H_

#include <string>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/views/view.h"

namespace views {
class ImageButton;
class ImageView;
class Label;
}  // namespace views

// One row of the Today list (issue #177): favicon, title and a close button
// that shows on hover or focus. A left click selects the tab, a middle click
// closes it. The row says "treeitem" to a screen reader. It holds no tab: the
// sidebar view tells it what to show.
class AerieSidebarRowView : public views::View {
  METADATA_HEADER(AerieSidebarRowView, views::View)

 public:
  struct Callbacks {
    base::RepeatingClosure select;
    base::RepeatingClosure close;
  };

  explicit AerieSidebarRowView(Callbacks callbacks);
  AerieSidebarRowView(const AerieSidebarRowView&) = delete;
  AerieSidebarRowView& operator=(const AerieSidebarRowView&) = delete;
  ~AerieSidebarRowView() override;

  void SetTitle(const std::u16string& title);
  void SetFavicon(const ui::ImageModel& favicon);
  void SetSelected(bool selected);
  bool selected() const { return selected_; }

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  void Layout(PassKey) override;
  bool OnMousePressed(const ui::MouseEvent& event) override;
  void OnMouseReleased(const ui::MouseEvent& event) override;
  void OnMouseEntered(const ui::MouseEvent& event) override;
  void OnMouseExited(const ui::MouseEvent& event) override;
  bool OnKeyPressed(const ui::KeyEvent& event) override;
  void OnFocus() override;
  void OnBlur() override;
  void OnThemeChanged() override;

 private:
  void UpdateState();

  Callbacks callbacks_;
  bool selected_ = false;
  bool hovered_ = false;
  raw_ptr<views::ImageView> favicon_ = nullptr;
  raw_ptr<views::Label> title_ = nullptr;
  raw_ptr<views::ImageButton> close_button_ = nullptr;
};

#endif  // CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_ROW_VIEW_H_
