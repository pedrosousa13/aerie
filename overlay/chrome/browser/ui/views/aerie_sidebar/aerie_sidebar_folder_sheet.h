// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_FOLDER_SHEET_H_
#define CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_FOLDER_SHEET_H_

#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/timer/timer.h"
#include "components/aerie_design_tokens/aerie_motion.h"
#include "components/aerie_sidebar/aerie_sidebar_palette.h"
#include "components/aerie_tab_model/tab_model.h"
#include "components/aerie_tab_model/tab_model_observer.h"
#include "components/prefs/pref_change_registrar.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/textfield/textfield_controller.h"

class AerieSidebarSwitch;
class PrefService;

namespace ui {
class Layer;
}  // namespace ui

namespace views {
class BoxLayoutView;
class Combobox;
class Label;
class LabelButton;
class Textfield;
}  // namespace views

// The folder editor sheet (issue #139, docs/sidebar-design.md, "Folders"): one
// panel next to the sidebar in which a folder's name, kind and rules are edited.
// Every edit is written to the Tab Model at once; there is no Save. The panel
// is a dialog for a screen reader ("Edit folder <name>"), keeps Tab inside it,
// closes on Esc and on Done, and gives focus back to the folder's row.
//
// Kinds: Manual and Auto-filing, from a table (`kKinds` in the .cc), so a
// later Smart or Behaviour kind is one more entry. Rules are chips: a switch,
// the type and text (press it to edit it) and a remove button. The footer holds
// the switch of every folder's rules, which is the same preference as the one
// in Settings.
//
// Motion: it opens on `sheet-enter` (slide 10 px from the sidebar's edge and
// fade, `decelerate`) and closes on `sheet-exit` (fade, `accelerate`). The
// slide is the transform of the contents' layer and the fade the opacity of
// the window's layer; nothing else animates. Reduced motion: opacity only,
// 90 ms.
class AerieSidebarFolderSheet : public views::BubbleDialogDelegate,
                                public views::TextfieldController,
                                public aerie_tab_model::TabModelObserver {
 public:
  struct Params {
    raw_ptr<views::View> anchor = nullptr;
    raw_ptr<aerie_tab_model::TabModel> model = nullptr;
    raw_ptr<PrefService> prefs = nullptr;
    std::string folder_id;
    aerie_sidebar::SidebarPalette palette;
    // The sidebar is on the right of the window: the sheet opens to its left.
    bool sidebar_on_right = false;
    // Runs once when the sheet has closed, with whether focus should go back to
    // the folder row (it should after Esc and Done, not after a click away).
    base::OnceCallback<void(bool refocus_anchor)> closed;
  };

  // Opens the sheet. Null when the folder does not exist.
  static AerieSidebarFolderSheet* Show(Params params);

  AerieSidebarFolderSheet(const AerieSidebarFolderSheet&) = delete;
  AerieSidebarFolderSheet& operator=(const AerieSidebarFolderSheet&) = delete;
  ~AerieSidebarFolderSheet() override;

  base::WeakPtr<AerieSidebarFolderSheet> GetWeakPtr() {
    return weak_factory_.GetWeakPtr();
  }
  const std::string& folder_id() const { return folder_id_; }

  // Closes the sheet with the exit motion; `refocus` as for `Params::closed`.
  void Dismiss(bool refocus);

  // The parts, for tests.
  views::Textfield* name_field() const { return name_field_; }
  // The buttons of the kind control (a segmented control; each says `radio`).
  views::LabelButton* kind_button(size_t index) const;
  // Empty, hidden containers for the body of a Smart folder (#140) and the
  // Behaviour section (#141). Nothing is in them yet.
  views::View* smart_body_slot() const { return smart_body_slot_; }
  views::View* behaviour_slot() const { return behaviour_slot_; }
  bool kind_checked(size_t index) const;
  views::BoxLayoutView* rules_list() const { return rules_list_; }
  views::BoxLayoutView* exceptions_list() const { return exceptions_list_; }
  views::Combobox* type_combobox() const { return type_combobox_; }
  views::Textfield* value_field() const { return value_field_; }
  views::LabelButton* add_button() const { return add_button_; }
  views::LabelButton* done_button() const { return done_button_; }
  AerieSidebarSwitch* all_switch() const { return all_switch_; }
  AerieSidebarSwitch* global_switch() const { return global_switch_; }
  // Finishes a pending rebuild now (tests).
  void FlushForTesting();
  // The layers of the open and close motion.
  ui::Layer* slide_layer() const;
  ui::Layer* fade_layer() const;

  // views::BubbleDialogDelegate:
  bool ShouldShowCloseButton() const override { return false; }
  // Esc anywhere in the sheet: close with the exit motion and give focus back
  // to the folder row.
  bool Cancel() override;
  // views::TextfieldController:
  void ContentsChanged(views::Textfield* sender,
                       const std::u16string& new_contents) override;
  bool HandleKeyEvent(views::Textfield* sender,
                      const ui::KeyEvent& key_event) override;
  // aerie_tab_model::TabModelObserver:
  void OnFolderChanged(const aerie_tab_model::FolderRecord& folder) override;
  void OnFolderRemoved(const aerie_tab_model::FolderRecord& folder) override;

 private:
  explicit AerieSidebarFolderSheet(Params params);
  void Build();
  void ScheduleRebuild();
  void Rebuild();
  void RebuildRules(const aerie_tab_model::FolderRecord& folder);
  void RebuildExceptions(const aerie_tab_model::FolderRecord& folder);
  void SetKind(size_t index);
  void SubmitRule();
  void BeginEditRule(const std::string& rule_id);
  void PlayOpen();
  void FinishClose();
  void OnClosing();

  const std::string folder_id_;
  raw_ptr<aerie_tab_model::TabModel> model_;
  raw_ptr<PrefService> prefs_;
  aerie_sidebar::SidebarPalette palette_;
  const bool sidebar_on_right_;
  base::OnceCallback<void(bool)> closed_;
  bool refocus_ = false;
  bool closing_ = false;
  bool rebuild_pending_ = false;
  std::string editing_rule_id_;

  raw_ptr<views::View> contents_ = nullptr;
  raw_ptr<views::Textfield> name_field_ = nullptr;
  std::vector<raw_ptr<views::LabelButton>> kind_buttons_;
  raw_ptr<views::View> smart_body_slot_ = nullptr;
  raw_ptr<views::View> behaviour_slot_ = nullptr;
  raw_ptr<views::View> auto_section_ = nullptr;
  raw_ptr<views::BoxLayoutView> rules_list_ = nullptr;
  raw_ptr<views::Combobox> type_combobox_ = nullptr;
  raw_ptr<views::Textfield> value_field_ = nullptr;
  raw_ptr<views::LabelButton> add_button_ = nullptr;
  raw_ptr<views::Label> rules_full_ = nullptr;
  raw_ptr<views::BoxLayoutView> exceptions_list_ = nullptr;
  raw_ptr<AerieSidebarSwitch> all_switch_ = nullptr;
  raw_ptr<AerieSidebarSwitch> global_switch_ = nullptr;
  raw_ptr<views::LabelButton> done_button_ = nullptr;

  aerie::motion::MotionHandle slide_motion_;
  aerie::motion::MotionHandle fade_motion_;
  base::OneShotTimer close_timer_;
  PrefChangeRegistrar pref_registrar_;
  base::ScopedObservation<aerie_tab_model::TabModel,
                          aerie_tab_model::TabModelObserver>
      model_observation_{this};
  base::WeakPtrFactory<AerieSidebarFolderSheet> weak_factory_{this};
};

#endif  // CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_FOLDER_SHEET_H_
