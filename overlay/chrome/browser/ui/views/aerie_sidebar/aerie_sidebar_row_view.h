// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_ROW_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_ROW_VIEW_H_

#include <memory>
#include <string>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "components/aerie_design_tokens/aerie_motion.h"
#include "components/aerie_sidebar/aerie_sidebar_palette.h"
#include "components/aerie_sidebar/aerie_sidebar_today_list.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/base/mojom/menu_source_type.mojom-forward.h"
#include "ui/gfx/animation/animation_delegate.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/views/context_menu_controller.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/view.h"

class AerieSidebarRenameField;
class PrefService;

namespace gfx {
class SlideAnimation;
}  // namespace gfx

namespace views {
class ImageButton;
class ImageView;
class Label;
class Textfield;
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
//
// Rename (issue #145): a tab with a name of its own shows a pencil after the
// title (SetRenamed); the pencil resets the name. BeginRename swaps a text
// field in for the title, with a caption under it. Enter keeps the name, Esc
// cancels and focus loss keeps it too. While the field is open the row handles
// no key and no mouse press: the field's editing keys (Backspace among them)
// never reach the row's own handlers.
class AerieSidebarRowView : public views::View,
                            public views::ContextMenuController,
                            public views::TextfieldController,
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
    // Dragging the row to reorder it (issue #215, item 8). The row knows only
    // the pointer; the sidebar knows the other rows.
    // `drag_begin` asks whether a drag may start (it may not while rows are
    // opening or closing). `drag_update` gets the pointer's travel along the
    // list in px. `drag_end` gets it again, whether the drag was cancelled
    // (the mouse capture was lost) and whether the pointer was released
    // outside the window.
    base::RepeatingCallback<bool()> drag_begin;
    base::RepeatingCallback<void(int dy)> drag_update;
    base::RepeatingCallback<void(int dy, bool cancelled, bool outside)>
        drag_end;
    // The context menu was asked for, with the pointer or, from the Menu key,
    // the row's own place (screen coordinates).
    base::RepeatingCallback<void(const gfx::Point& screen_point,
                                 ui::mojom::MenuSourceType source)>
        context_menu;
    // The "Back to pinned page" button of a pinned row was pressed (issue
    // #180).
    base::RepeatingClosure back_to_pinned;
    // Rename (issue #145). `begin_rename`: a double-click on the title or F2.
    // `commit_rename` gets the text of the field when the user keeps it (Enter
    // or focus loss); the sidebar normalizes it and writes the Tab Model.
    // `reset_name`: the pencil was pressed.
    base::RepeatingClosure begin_rename;
    base::RepeatingCallback<void(const std::u16string& text)> commit_rename;
    base::RepeatingClosure reset_name;
  };

  AerieSidebarRowView(Callbacks callbacks,
                      const aerie_sidebar::SidebarPalette& palette);
  AerieSidebarRowView(const AerieSidebarRowView&) = delete;
  AerieSidebarRowView& operator=(const AerieSidebarRowView&) = delete;
  ~AerieSidebarRowView() override;

  // The colours of the row: text, the close icon, the hover and selected
  // backgrounds and the focus ring (issue #215, item 7). Called again when the
  // theme, the colour mode or the Workspace colour changes.
  void SetPalette(const aerie_sidebar::SidebarPalette& palette);
  const aerie_sidebar::SidebarPalette& palette() const { return palette_; }

  void SetTitle(const std::u16string& title);
  void SetFavicon(const ui::ImageModel& favicon);
  void SetSelected(bool selected);
  bool selected() const { return selected_; }

  // A sleeping tab (issue #179): the favicon at `motion-opacity-sleeping` and,
  // after the fade, a static desaturate; a moon badge on the favicon; and
  // ", asleep" at the end of the accessible name. `reduced_motion_prefs` is the
  // prefs that say whether to reduce motion: when it is set, a change from
  // awake to asleep fades the favicon (opacity only) on `fast` and `standard`;
  // reduced motion, a null `reduced_motion_prefs` and the first call all swap
  // at once.
  void SetSleeping(bool sleeping, const PrefService* reduced_motion_prefs);
  bool sleeping() const { return sleeping_; }
  // For tests.
  views::ImageView* favicon_view() const { return favicon_; }
  views::ImageView* sleep_badge() const { return sleep_badge_; }

  // A pinned row that has left its pinned page shows the "Back to pinned page"
  // button between the title and the close button (issue #180).
  void SetBackToPinnedVisible(bool visible);
  views::ImageButton* back_to_pinned_button() const {
    return back_to_pinned_;
  }
  // A tab with a name of its own (issue #145): the pencil after the title, with
  // `pencil_tooltip` ("Page title: ... Shown: your name." and the order), and
  // ", renamed" at the end of the accessible name, after ", asleep". When the
  // row goes from not renamed to renamed and `reduced_motion_prefs` is set, the
  // pencil fades in on `xfast` (opacity only); the first call and a null
  // `reduced_motion_prefs` show it at once.
  void SetRenamed(bool renamed,
                  const std::u16string& pencil_tooltip,
                  const PrefService* reduced_motion_prefs);
  bool renamed() const { return renamed_; }
  views::ImageButton* pencil_button() const { return pencil_; }
  views::Label* title_view() const { return title_; }

  // Opens the field in place of the title with `initial_text`, all of it
  // selected, and puts keyboard focus in it. The title view fades out and the
  // field fades in on `xfast`, opacity only. Does nothing while the row closes
  // or already edits.
  void BeginRename(const std::u16string& initial_text,
                   const PrefService* reduced_motion_prefs);
  bool renaming() const { return renaming_; }
  // Closes the field without keeping the text (the tab went away, say).
  void CancelRename();
  AerieSidebarRenameField* rename_field() const;
  views::Label* rename_caption() const { return rename_caption_; }

  // The tooltip, and the accessible name, of the close button. The default is
  // "Close tab"; a pinned row names the pinned-close action ("Reset and sleep",
  // "Sleep" or "Close").
  void SetCloseTooltip(const std::u16string& text);
  views::ImageButton* close_button() const { return close_button_; }

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
  // Drag motion (item 8). All of it is `transform` of a layer, and nothing else:
  // the row's own layer carries the pointer offset and the shift, and the
  // content layer carries the lift. The layers exist only for the drag.
  bool dragging() const { return dragging_; }
  // Raises the row and plays the lift (scale 1.02 on `fast`; reduced motion
  // does not scale).
  void BeginLift(const PrefService* prefs);
  // Puts the row `dy` px from its slot at once (the row follows the pointer,
  // also under reduced motion; this is a property set, not an animation).
  void SetDragOffset(int dy);
  // Moves the row to `dy` px from its slot with the shift animation (`base`,
  // `standard`). Reduced motion sets it at once.
  void ShiftTo(int dy, const PrefService* prefs);
  // The drop: the row goes from `from_dy` to its slot on the spring while the
  // lift goes back; reduced motion sets everything at once. The layers are
  // dropped when the motion is over.
  void Settle(int from_dy, const PrefService* prefs);
  // Puts the row at its slot at once and drops the drag layers (the other rows
  // of a drop, whose slots took over their shift).
  void ResetDrag();
  // The row's current offset from its slot, in px, for tests.
  int drag_offset() const { return drag_offset_; }
  // True while a drag layer exists (the drag, or a settle in progress).
  bool has_drag_layers() const { return drag_layers_; }

  // True while the row height animates (the layout exception).
  bool height_animating() const;
  // The view that holds the row's drawing; its layer is the one that animates.
  views::View* content() const { return content_; }

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  void Layout(PassKey) override;
  bool OnMousePressed(const ui::MouseEvent& event) override;
  bool OnMouseDragged(const ui::MouseEvent& event) override;
  void OnMouseReleased(const ui::MouseEvent& event) override;
  void OnMouseCaptureLost() override;
  void OnMouseEntered(const ui::MouseEvent& event) override;
  void OnMouseExited(const ui::MouseEvent& event) override;
  bool OnKeyPressed(const ui::KeyEvent& event) override;
  bool SkipDefaultKeyEventProcessing(const ui::KeyEvent& event) override;
  // Esc while a drag runs cancels it (issue #215, item 8).
  bool AcceleratorPressed(const ui::Accelerator& accelerator) override;
  std::u16string GetRenderedTooltipText(const gfx::Point& p) const override;
  // views::ContextMenuController:
  void ShowContextMenuForViewImpl(
      views::View* source,
      const gfx::Point& point,
      ui::mojom::MenuSourceType source_type) override;
  void OnFocus() override;
  void OnBlur() override;
  void OnThemeChanged() override;

  // views::TextfieldController:
  void ContentsChanged(views::Textfield* sender,
                       const std::u16string& new_contents) override;
  bool HandleKeyEvent(views::Textfield* sender,
                      const ui::KeyEvent& key_event) override;
  // gfx::AnimationDelegate:
  void AnimationProgressed(const gfx::Animation* animation) override;
  void AnimationEnded(const gfx::Animation* animation) override;

 private:
  void UpdateState();
  void UpdateName();
  // Shows or hides the pencil, the back button and the close button for the
  // row's state (renaming hides them all).
  void UpdateTrailing();
  void EnsureRenameField();
  void StyleRenameField();
  // Closes the field. `commit`: hand its text to `commit_rename`. `refocus`:
  // put keyboard focus back on the row (Enter and Esc; not a focus loss).
  void EndRename(bool commit, bool refocus);
  void RunCommitRename(const std::u16string& text);
  void UpdateCaptionHeight();
  void OnRenameBlur();
  void ApplySleepLook(bool animate, const PrefService* prefs);
  // Starts the height animation toward `target` (1 grows, 0 collapses).
  void AnimateHeight(bool grow, int duration_ms, gfx::Tween::Type tween);
  void FinishHeightAnimation();
  void CollapseAfterContent();
  void EnsureDragLayers();
  void DropDragLayers();
  // Ends the drag the pointer started: stops listening for Esc and tells the
  // sidebar. `cancelled`: the drag was abandoned (Esc or lost capture).
  void FinishDrag(int dy, bool cancelled, bool outside);
  gfx::Transform LiftTransform(float scale) const;

  Callbacks callbacks_;
  aerie_sidebar::SidebarPalette palette_;
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
  // Dragging.
  bool pressed_ = false;
  bool dragging_ = false;
  // Esc ended the drag; the release that follows is not a click.
  bool drag_cancelled_ = false;
  gfx::Point press_point_;
  int last_dy_ = 0;
  int drag_offset_ = 0;
  bool drag_layers_ = false;
  aerie::motion::MotionHandle row_motion_;
  aerie::motion::MotionHandle lift_motion_;
  base::OneShotTimer settle_timer_;
  raw_ptr<views::ImageView> favicon_ = nullptr;
  raw_ptr<views::ImageView> sleep_badge_ = nullptr;
  std::u16string title_text_;
  bool sleeping_ = false;
  // The first SetSleeping sets the look at once, however the row came to be.
  bool sleep_look_applied_ = false;
  aerie::motion::MotionHandle sleep_motion_;
  base::OneShotTimer desaturate_timer_;
  raw_ptr<views::Label> title_ = nullptr;
  raw_ptr<views::ImageButton> close_button_ = nullptr;
  raw_ptr<views::ImageButton> back_to_pinned_ = nullptr;
  bool back_to_pinned_wanted_ = false;

  // Rename (issue #145).
  raw_ptr<views::ImageButton> pencil_ = nullptr;
  raw_ptr<views::Label> rename_caption_ = nullptr;
  raw_ptr<AerieSidebarRenameField> rename_field_ = nullptr;
  bool renamed_ = false;
  bool renamed_applied_ = false;
  bool renaming_ = false;
  // A double-click began the rename: its release must not select the tab (that
  // would take focus from the field).
  bool suppress_release_ = false;
  aerie::motion::MotionHandle pencil_motion_;
  aerie::motion::MotionHandle field_motion_;
  aerie::motion::MotionHandle title_motion_;
  // The height the caption needs under the field at the field's width.
  int caption_height_ = 0;

  base::WeakPtrFactory<AerieSidebarRowView> weak_factory_{this};
};

#endif  // CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_ROW_VIEW_H_
