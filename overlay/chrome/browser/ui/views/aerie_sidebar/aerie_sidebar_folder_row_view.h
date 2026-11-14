// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_FOLDER_ROW_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_FOLDER_ROW_VIEW_H_

#include <string>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_row_view.h"
#include "components/aerie_design_tokens/aerie_motion.h"
#include "ui/base/metadata/metadata_header_macros.h"

namespace views {
class ImageButton;
}  // namespace views

// The row of a folder (issues #181, #139 and #228). It is a row of the same
// class as a tab row, so that the rename (double-click, F2, the menu) and the
// keys work the same way, with what only a folder has: a chevron, the folder
// glyph in the favicon's place, the dot of a shut folder that holds the open
// tab, the funnel of an auto-filing folder and the pulse of a tab filed into
// it. It has no close button, no pencil and no sleep badge.
class AerieSidebarFolderRowView : public AerieSidebarRowView {
  METADATA_HEADER(AerieSidebarFolderRowView, AerieSidebarRowView)

 public:
  AerieSidebarFolderRowView(Callbacks callbacks,
                            const aerie_sidebar::SidebarPalette& palette);
  AerieSidebarFolderRowView(const AerieSidebarFolderRowView&) = delete;
  AerieSidebarFolderRowView& operator=(const AerieSidebarFolderRowView&) =
      delete;
  ~AerieSidebarFolderRowView() override;

  // The folder row of `row`, or null when `row` is null or a tab row.
  static AerieSidebarFolderRowView* From(AerieSidebarRowView* row);

  // What a folder row shows: its name, its accessible name (for example
  // "Trips, folder, expanded, 3 items"), whether it is open, and the dot that
  // says a shut folder holds the open tab. `motion_prefs` set: a change of
  // `expanded` turns the chevron with the motion (a transform of its layer);
  // null, and the first call, set the chevron at once with no layer.
  void SetFolderState(const std::u16string& name,
                      const std::u16string& accessible_name,
                      bool expanded,
                      bool holds_selected_tab,
                      const PrefService* motion_prefs);
  bool expanded() const { return expanded_; }
  views::View* chevron_view() const;

  // Auto-filing folders (issue #139). An auto-filing folder shows a funnel after
  // its name; it opens the folder editor. `tooltip` names it ("Edit folder
  // rules").
  void SetAutoFiling(bool auto_filing, const std::u16string& tooltip);
  views::ImageButton* funnel_button() const { return funnel_; }
  // A tab was filed into this folder: a wash of the accent colour rises over the
  // row and falls back (`autofile-pulse`: 700 ms, 300 ms under reduced motion;
  // opacity only). `prefs` says whether motion is reduced.
  void PlayFilingPulse(const PrefService* prefs);
  views::View* pulse_view() const { return pulse_; }
  views::View* selection_dot() const { return selection_dot_; }

  // AerieSidebarRowView:
  bool is_folder() const override;

 protected:
  void ApplyKindPalette() override;
  bool ShowsCloseButton() const override;
  std::u16string AccessibleName() const override;
  int RenameFieldNameId() const override;
  int RenameCaptionId() const override;
  bool IsEditFolderClick(const ui::MouseEvent& event) const override;
  void LayoutKindViews() override;
  void OnDensityChanged() override;

 private:
  bool auto_filing_ = false;
  bool holds_selected_tab_ = false;
  void EndChevronTurn();

  bool expanded_ = false;
  bool folder_state_applied_ = false;
  std::u16string folder_accessible_name_;
  raw_ptr<views::ImageView> chevron_ = nullptr;
  raw_ptr<views::View> selection_dot_ = nullptr;
  raw_ptr<views::ImageButton> funnel_ = nullptr;
  raw_ptr<views::View> pulse_ = nullptr;
  aerie::motion::MotionHandle pulse_motion_;
  base::OneShotTimer pulse_timer_;
  aerie::motion::MotionHandle chevron_motion_;
  base::OneShotTimer chevron_timer_;
  base::WeakPtrFactory<AerieSidebarFolderRowView> folder_weak_factory_{this};
};

#endif  // CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_FOLDER_ROW_VIEW_H_
