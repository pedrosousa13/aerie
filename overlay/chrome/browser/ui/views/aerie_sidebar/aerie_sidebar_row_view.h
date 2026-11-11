// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_ROW_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_ROW_VIEW_H_

#include <memory>
#include <string>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/timer/timer.h"
#include "components/aerie_design_tokens/aerie_motion.h"
#include "components/aerie_sidebar/aerie_sidebar_today_list.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/gfx/animation/animation_delegate.h"
#include "ui/views/view.h"

class PrefService;

namespace gfx {
class SlideAnimation;
}  // namespace gfx

namespace views {
class ImageButton;
class ImageView;
class Label;
}  // namespace views

// One row of the Today list (issue #177): favicon, title and a close button
// that shows on hover or focus. A left click selects the tab, a middle click
// closes it. The row says "treeitem" to a screen reader. It holds no tab: the
// sidebar view tells it what to show.
//
// Keyboard (issue #215): the row is one stop of a roving tab stop; only the row
// that SetTabStop(true) marks is reached with Tab. Up, Down, Home and End are
// handed to the sidebar, which moves the stop (HandleListKey); Delete and
// Ctrl+W (Cmd+W on macOS) close the row's own tab, not the selected one.
//
// Motion (issue #215): the row has a content view that holds everything that
// is drawn. Opening scales and fades the content in (transform and opacity,
// through aerie::motion::AnimateLayer) while the row height grows; closing
// slides and fades the content out, then collapses the height. The row height
// is the one property that lays out while it animates, and only this list
// is affected (docs/sidebar-design.md, "Performance rules"). Under reduced
// motion the content only fades and the height snaps. A row that is not
// animated has no layer.
class AerieSidebarRowView : public views::View,
                            public gfx::AnimationDelegate {
  METADATA_HEADER(AerieSidebarRowView, views::View)

 public:
  struct Callbacks {
    base::RepeatingClosure select;
    base::RepeatingClosure close;
    // A key of the Today tree that the row does not handle itself.
    base::RepeatingCallback<void(aerie_sidebar::ListKey)> key;
    // The row took keyboard focus.
    base::RepeatingClosure focused;
  };

  explicit AerieSidebarRowView(Callbacks callbacks);
  AerieSidebarRowView(const AerieSidebarRowView&) = delete;
  AerieSidebarRowView& operator=(const AerieSidebarRowView&) = delete;
  ~AerieSidebarRowView() override;

  void SetTitle(const std::u16string& title);
  void SetFavicon(const ui::ImageModel& favicon);
  void SetSelected(bool selected);
  bool selected() const { return selected_; }

  // Makes this row the Tab stop of the list (or not). Every row can still take
  // focus from the arrow keys: the sidebar moves the stop first.
  void SetTabStop(bool tab_stop);
  bool is_tab_stop() const;

  // Plays the open motion. `prefs` is for reduced motion.
  void PlayOpen(const PrefService* prefs);
  // Plays the close motion, then runs `done` once the row has collapsed. The
  // row takes no input from here on. `outer_edge_sign` is -1 for a sidebar on
  // the left, +1 on the right.
  void PlayClose(const PrefService* prefs,
                 int outer_edge_sign,
                 base::OnceClosure done);
  bool is_closing() const { return closing_; }
  // True while the row height animates (the layout exception).
  bool height_animating() const;
  // The view that holds the row's drawing; its layer is the one that animates.
  views::View* content() const { return content_; }

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  void Layout(PassKey) override;
  bool OnMousePressed(const ui::MouseEvent& event) override;
  void OnMouseReleased(const ui::MouseEvent& event) override;
  void OnMouseEntered(const ui::MouseEvent& event) override;
  void OnMouseExited(const ui::MouseEvent& event) override;
  bool OnKeyPressed(const ui::KeyEvent& event) override;
  bool SkipDefaultKeyEventProcessing(const ui::KeyEvent& event) override;
  std::u16string GetRenderedTooltipText(const gfx::Point& p) const override;
  void OnFocus() override;
  void OnBlur() override;
  void OnThemeChanged() override;

  // gfx::AnimationDelegate:
  void AnimationProgressed(const gfx::Animation* animation) override;
  void AnimationEnded(const gfx::Animation* animation) override;

 private:
  void UpdateState();
  // Starts the height animation toward `target` (1 grows, 0 collapses).
  void AnimateHeight(bool grow, int duration_ms, gfx::Tween::Type tween);
  void FinishHeightAnimation();
  void CollapseAfterContent();

  Callbacks callbacks_;
  bool selected_ = false;
  bool hovered_ = false;
  bool closing_ = false;
  // The share of the full row height that the row takes (1 at rest).
  double height_fraction_ = 1.0;
  raw_ptr<views::View> content_ = nullptr;
  std::unique_ptr<gfx::SlideAnimation> height_animation_;
  aerie::motion::MotionHandle content_motion_;
  base::OneShotTimer collapse_timer_;
  base::OnceClosure closed_;
  bool close_collapses_ = false;
  int close_height_ms_ = 0;
  raw_ptr<views::ImageView> favicon_ = nullptr;
  raw_ptr<views::Label> title_ = nullptr;
  raw_ptr<views::ImageButton> close_button_ = nullptr;
};

#endif  // CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_ROW_VIEW_H_
