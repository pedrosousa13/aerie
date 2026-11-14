// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_ARCHIVE_PANEL_H_
#define CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_ARCHIVE_PANEL_H_

#include <memory>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "components/aerie_design_tokens/aerie_motion.h"
#include "components/aerie_sidebar/aerie_sidebar_palette.h"
#include "components/aerie_tab_model/archive.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/textfield/textfield_controller.h"

class PrefService;

namespace views {
class BoxLayoutView;
class LabelButton;
class MenuRunner;
class ScrollView;
class Textfield;
}  // namespace views

class AerieSidebarArchiveRow;

// The Archive panel (issue #183): a popover above the Archive button. A header
// ("Archive Today tabs after <time>" with a menu of the four times), a search
// field, and the archived tabs in groups (Today, Yesterday, Last week,
// Earlier), each row with its Workspace. A click or Enter on a row restores it.
//
// Accessibility: the panel is a dialog named "Archive"; the search is a
// searchbox; the list is a listbox of listbox options. Up and Down move
// between the search and the rows, Enter restores, Esc closes. Focus goes back
// to the Archive button when the panel closes.
//
// Motion: it drops 4 px and fades in on `fast` and `decelerate`; there is no
// closing animation. Reduced motion: opacity only, 90 ms.
class AerieSidebarArchivePanel : public views::BubbleDialogDelegate,
                                 public views::TextfieldController,
                                 public ui::SimpleMenuModel::Delegate {
 public:
  class Host {
   public:
    // Every archived tab, newest first.
    virtual std::vector<aerie_tab_model::ArchivedRow> ArchivedRows() const = 0;
    virtual std::u16string WorkspaceName(
        const std::string& workspace_id) const = 0;
    // The pref value of the timeout: "12h", "24h", "7d" or "never".
    virtual std::string ArchiveAfter() const = 0;
    virtual void SetArchiveAfter(const std::string& value) = 0;
    virtual base::Time Now() const = 0;
    virtual const PrefService* Prefs() const = 0;
    // The row was chosen. `row_in_screen` is where it was on screen, for the
    // copy that flies into Today. The panel closes itself first.
    virtual void RestoreArchived(const aerie_tab_model::ArchivedRow& row,
                                 const gfx::Rect& row_in_screen) = 0;
    // The panel went away. `refocus_button` is true after Esc, Enter, a choice,
    // the button's own click and a click outside (the default); it is false only
    // when the sidebar is torn down.
    virtual void OnArchivePanelClosed(bool refocus_button) = 0;

   protected:
    ~Host() = default;
  };

  static AerieSidebarArchivePanel* Show(
      views::View* anchor,
      Host* host,
      const aerie_sidebar::SidebarPalette& palette);

  AerieSidebarArchivePanel(const AerieSidebarArchivePanel&) = delete;
  AerieSidebarArchivePanel& operator=(const AerieSidebarArchivePanel&) = delete;
  ~AerieSidebarArchivePanel() override;

  base::WeakPtr<AerieSidebarArchivePanel> GetWeakPtr() {
    return weak_factory_.GetWeakPtr();
  }
  views::Textfield* search() const { return search_; }
  views::BoxLayoutView* list() const { return list_; }
  views::LabelButton* timeout_button() const { return timeout_button_; }
  // The rows now shown, in order.
  std::vector<AerieSidebarArchiveRow*> rows() const;
  // The headings now shown, in order.
  std::vector<std::u16string> group_titles() const;
  // The popover animation, for tests.
  const aerie::motion::MotionHandle& open_motion() const {
    return open_motion_;
  }
  // The timeout menu, opened from the header button.
  void ShowTimeoutMenu();
  // Restores the row `index` (as a click does).
  void ChooseRow(size_t index);
  // A click on a row: the callback of the row binds the row itself.
  void OnRowPressed(AerieSidebarArchiveRow* row, const ui::Event& event);
  void RestoreRow(AerieSidebarArchiveRow* row);
  void ClosePanel(bool refocus_button);

  // views::TextfieldController:
  void ContentsChanged(views::Textfield* sender,
                       const std::u16string& new_contents) override;
  bool HandleKeyEvent(views::Textfield* sender,
                      const ui::KeyEvent& key_event) override;

  // ui::SimpleMenuModel::Delegate:
  bool IsCommandIdChecked(int command_id) const override;
  void ExecuteCommand(int command_id, int event_flags) override;

  // Moves focus along the rows; called by a row on Up and Down.
  void FocusNeighbour(AerieSidebarArchiveRow* from, int direction);

 private:
  AerieSidebarArchivePanel(views::View* anchor,
                           Host* host,
                           const aerie_sidebar::SidebarPalette& palette);
  void BuildList();
  void UpdateTimeoutLabel();
  void OnClosing();

  const raw_ptr<Host> host_;
  const aerie_sidebar::SidebarPalette palette_;
  raw_ptr<views::Textfield> search_ = nullptr;
  raw_ptr<views::BoxLayoutView> list_ = nullptr;
  raw_ptr<views::ScrollView> scroll_ = nullptr;
  raw_ptr<views::LabelButton> timeout_button_ = nullptr;
  std::unique_ptr<ui::SimpleMenuModel> menu_model_;
  std::unique_ptr<views::MenuRunner> menu_runner_;
  std::vector<aerie_tab_model::ArchivedRow> shown_;
  std::u16string query_;
  bool closed_ = false;
  bool refocus_ = true;
  aerie::motion::MotionHandle open_motion_;
  base::WeakPtrFactory<AerieSidebarArchivePanel> weak_factory_{this};
};

// One archived tab in the panel. A button (a click, Enter or Space restores
// it), exposed as a listbox option.
class AerieSidebarArchiveRow : public views::Button {
  METADATA_HEADER(AerieSidebarArchiveRow, views::Button)

 public:
  AerieSidebarArchiveRow(const aerie_tab_model::ArchivedRow& row,
                         const std::u16string& workspace_name,
                         const aerie_sidebar::SidebarPalette& palette,
                         PressedCallback callback,
                         AerieSidebarArchivePanel* panel);
  ~AerieSidebarArchiveRow() override;

  const aerie_tab_model::ArchivedRow& archived() const { return row_; }
  const std::u16string& title() const { return title_text_; }
  const std::u16string& workspace() const { return workspace_text_; }

  // views::Button:
  bool OnKeyPressed(const ui::KeyEvent& event) override;
  void OnPaintBackground(gfx::Canvas* canvas) override;
  void StateChanged(ButtonState old_state) override;
  void OnFocus() override;
  void OnBlur() override;

 private:
  const aerie_tab_model::ArchivedRow row_;
  std::u16string title_text_;
  std::u16string workspace_text_;
  aerie_sidebar::SidebarPalette palette_;
  const raw_ptr<AerieSidebarArchivePanel> panel_;
};

#endif  // CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_ARCHIVE_PANEL_H_
