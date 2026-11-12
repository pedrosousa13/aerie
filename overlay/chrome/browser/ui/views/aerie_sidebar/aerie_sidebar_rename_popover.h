// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_RENAME_POPOVER_H_
#define CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_RENAME_POPOVER_H_

#include <optional>
#include <string>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "components/aerie_design_tokens/aerie_motion.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/textfield/textfield_controller.h"

class AerieSidebarRenameField;
class PrefService;

namespace views {
class Label;
}  // namespace views

// The small popover in which a Favorite is renamed (issue #145). A tile has no
// title, so there is nothing to edit in place: the popover holds the field and
// the same caption as a row, and opens beside the tile. Enter keeps the name,
// Esc cancels, and losing focus (a click elsewhere) keeps it, as in a row.
// The owner gets the result once: the text to keep, or nothing for a cancel,
// and whether focus should go back to the tile (it should after Enter and Esc,
// not after a click elsewhere).
//
// It is a BubbleDialogDelegate that the widget owns: the widget and the popover
// go together when it closes.
class AerieSidebarRenamePopover : public views::BubbleDialogDelegate,
                                  public views::TextfieldController {
 public:
  using DoneCallback =
      base::OnceCallback<void(std::optional<std::u16string> text,
                              bool refocus_anchor)>;

  // Opens the popover below the sidebar's edge, anchored at `anchor`.
  static AerieSidebarRenamePopover* Show(
      views::View* anchor,
      const std::u16string& initial_text,
      const PrefService* reduced_motion_prefs,
      DoneCallback done);

  AerieSidebarRenamePopover(const AerieSidebarRenamePopover&) = delete;
  AerieSidebarRenamePopover& operator=(const AerieSidebarRenamePopover&) =
      delete;
  ~AerieSidebarRenamePopover() override;

  base::WeakPtr<AerieSidebarRenamePopover> GetWeakPtr() {
    return weak_factory_.GetWeakPtr();
  }
  AerieSidebarRenameField* field() const { return field_; }
  views::Label* caption() const { return caption_; }
  // Closes the popover as a cancel.
  void CancelRename();

  // views::TextfieldController:
  void ContentsChanged(views::Textfield* sender,
                       const std::u16string& new_contents) override;
  bool HandleKeyEvent(views::Textfield* sender,
                      const ui::KeyEvent& key_event) override;

 private:
  AerieSidebarRenamePopover(views::View* anchor,
                            const std::u16string& initial_text,
                            DoneCallback done);
  void Finish(bool commit, bool refocus);
  void OnClosing();

  raw_ptr<AerieSidebarRenameField> field_ = nullptr;
  raw_ptr<views::Label> caption_ = nullptr;
  DoneCallback done_;
  bool finished_ = false;
  aerie::motion::MotionHandle field_motion_;
  base::WeakPtrFactory<AerieSidebarRenamePopover> weak_factory_{this};
};

#endif  // CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_RENAME_POPOVER_H_
