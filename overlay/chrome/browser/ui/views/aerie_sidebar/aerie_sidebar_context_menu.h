// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_CONTEXT_MENU_H_
#define CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_CONTEXT_MENU_H_

#include <memory>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "components/aerie_design_tokens/aerie_motion.h"
#include "components/aerie_tab_model/tab_model_types.h"
#include "ui/base/mojom/menu_source_type.mojom-forward.h"
#include "ui/gfx/geometry/point.h"
#include "ui/views/controls/menu/menu_delegate.h"

class PrefService;

namespace ui {
class Layer;
}  // namespace ui

namespace views {
class MenuItemView;
class MenuRunner;
class Widget;
}  // namespace views

// The context menu of a tab row or a Favorites tile of the sidebar (issues #179
// and #180). Its items, top to bottom: Pin tab or Unpin tab (a tab in Today or
// Pinned), Add to Favorites or Remove from Favorites, Sleep tab and Close tab.
// Sleep tab is disabled, with the reason in its tooltip, when the tab cannot
// sleep. Close tab follows the pinned-close setting for a Pinned or Favorites
// tab. Rename and, only when the tab has a name of its own, Reset name follow
// the section items (issue #145); later slices add the folder items to this
// menu. The menu is the views menu, so the keyboard works as in every menu:
// arrows, Enter and Esc.
class AerieSidebarContextMenu : public views::MenuDelegate {
 public:
  static constexpr int kCommandSleepTab = 1;
  static constexpr int kCommandPin = 2;
  static constexpr int kCommandUnpin = 3;
  static constexpr int kCommandAddToFavorites = 4;
  static constexpr int kCommandRemoveFromFavorites = 5;
  static constexpr int kCommandClose = 6;
  static constexpr int kCommandBackToPinned = 7;
  static constexpr int kCommandRename = 8;
  static constexpr int kCommandResetName = 9;
  // Folders (issue #181).
  static constexpr int kCommandNewFolder = 10;
  static constexpr int kCommandRenameFolder = 11;
  static constexpr int kCommandDeleteFolder = 12;
  static constexpr int kCommandMoveToNewFolder = 13;
  static constexpr int kCommandMoveOutOfFolder = 14;
  // The "Move to folder" item opens a list; its entries are
  // kCommandMoveToFolderBase plus their index in Options::move_targets.
  static constexpr int kCommandMoveToFolderList = 15;
  static constexpr int kCommandMoveToFolderBase = 100;

  // What the menu is for.
  enum class Target {
    // A tab row or a Favorites tile.
    kTab,
    // A folder row: New folder (inside it), Rename folder, Delete folder.
    kFolder,
    // The heading of a section: New folder only.
    kSection,
  };

  struct MoveTarget {
    std::string folder_id;
    // The folder's name with its parents', for example "Trips / Rome".
    std::u16string label;
    bool enabled = true;
  };

  struct SleepItem {
    bool enabled = false;
    // Why the item is disabled; empty when it is enabled.
    std::u16string tooltip;
  };

  struct Options {
    // Where the tab is: it decides which of the section items are shown.
    aerie_tab_model::Section section = aerie_tab_model::Section::kToday;
    // A pinned tab that has left its pinned page: the menu offers "Back to pinned
    // page" first, the keyboard way to the button of the row.
    bool away_from_pinned = false;
    // The tab has a name the user typed (issue #145): the menu then also offers
    // "Reset name".
    bool has_manual_name = false;
    SleepItem sleep;

    Target target = Target::kTab;
    // "New folder" (issue #181): in a tab's menu it makes the folder next to
    // the tab (in the tab's own folder), in a folder's menu inside the folder,
    // in a section's menu at the top of the section. Disabled, with the reason
    // in its tooltip, when it would nest deeper than 5 levels.
    bool show_new_folder = false;
    bool new_folder_enabled = true;
    std::u16string new_folder_tooltip;
    // A tab in Pinned or Today: "Move to new folder", and the "Move to folder"
    // list of `move_targets` (hidden when there is no folder to choose).
    bool show_move_to_new_folder = false;
    bool move_to_new_folder_enabled = true;
    std::u16string move_to_new_folder_tooltip;
    std::vector<MoveTarget> move_targets;
    // A tab that is in a folder: "Move out of folder".
    bool show_move_out = false;
  };

  // Runs with the command id when an enabled item is chosen.
  using CommandCallback = base::RepeatingCallback<void(int command)>;

  AerieSidebarContextMenu(Options options, CommandCallback on_command);
  AerieSidebarContextMenu(const AerieSidebarContextMenu&) = delete;
  AerieSidebarContextMenu& operator=(const AerieSidebarContextMenu&) = delete;
  ~AerieSidebarContextMenu() override;

  // Opens the menu at `screen_point`, with the open motion of issue #180: the
  // menu scales from 0.97 and fades in (`fast`, `decelerate`); reduced motion
  // fades it in only. `reduced_motion_prefs` is what says whether to reduce.
  void Run(views::Widget* parent,
           const gfx::Point& screen_point,
           ui::mojom::MenuSourceType source_type,
           const PrefService* reduced_motion_prefs = nullptr);
  bool is_running() const;
  // Closes the menu without choosing.
  void Close();

  // The folder that the "Move to folder" entry `command` stands for, or an
  // empty string.
  std::string FolderIdForCommand(int command) const;

  // The menu items, for tests. Null when the menu does not have the item.
  views::MenuItemView* sleep_item() const;
  views::MenuItemView* item(int command) const;
  // The layer the open motion plays on; null before Run or when the menu has
  // no layer.
  ui::Layer* open_layer_for_testing() const { return motion_layer_; }

  // views::MenuDelegate:
  bool IsCommandEnabled(int id) const override;
  void ExecuteCommand(int id) override;
  std::u16string GetTooltipText(int id,
                                const gfx::Point& screen_loc) const override;
  void OnMenuClosed(views::MenuItemView* menu) override;

 private:
  bool HasCommand(int id) const;
  void BuildTabItems();

  Options options_;
  CommandCallback on_command_;
  raw_ptr<views::MenuItemView> root_ = nullptr;
  std::unique_ptr<views::MenuRunner> runner_;
  aerie::motion::MotionHandle menu_motion_;
  raw_ptr<ui::Layer> motion_layer_ = nullptr;
};



#endif  // CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_CONTEXT_MENU_H_
