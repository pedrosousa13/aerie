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
  // The commands of the menu. The "Move to folder" list opens a menu of its
  // own: its entries are `kMoveToFolderBase` plus their index in
  // Options::move_targets (see MoveToFolderCommand).
  enum class Command {
    kSleepTab = 1,
    kPin = 2,
    kUnpin = 3,
    kAddToFavorites = 4,
    kRemoveFromFavorites = 5,
    kClose = 6,
    kBackToPinned = 7,
    kRename = 8,
    kResetName = 9,
    // Folders (issue #181).
    kNewFolder = 10,
    kRenameFolder = 11,
    kDeleteFolder = 12,
    kMoveToNewFolder = 13,
    kMoveOutOfFolder = 14,
    kMoveToFolderList = 15,
    // Auto-filing folders (issue #139): the folder's menu opens the editor; the
    // menu of a tab that a rule filed can stop that rule matching it.
    kEditFolder = 16,
    kStopRuleMatching = 17,
    kMoveToFolderBase = 100,
  };

  // The command of the "Move to folder" entry for `Options::move_targets[index]`.
  static Command MoveToFolderCommand(size_t index);

  // True while the menu is on screen (the auto-hide peek keeps the sidebar open
  // for it, issue #184), and a callback for when it closes.
  bool IsOpen() const;
  void set_closed_callback(base::RepeatingClosure closed) {
    closed_ = std::move(closed);
  }

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

  // How one optional item of the menu shows: whether the menu has it, whether
  // it can be chosen, and, when it cannot, the reason for its tooltip.
  struct ItemState {
    bool shown = false;
    bool enabled = true;
    std::u16string tooltip;
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
    // in its tooltip, when it would nest deeper than 5 levels. A folder's and a
    // section's menu always have it.
    ItemState new_folder;
    // A tab in Pinned or Today: "Move to new folder", and the "Move to folder"
    // list of `move_targets` (hidden when there is no folder to choose).
    ItemState move_to_new_folder;
    std::vector<MoveTarget> move_targets;
    // A tab that is in a folder: "Move out of folder".
    ItemState move_out;
    // A tab that a folder rule filed (issue #139): "Stop this rule matching this
    // tab".
    ItemState stop_rule;
  };

  // Runs with the command id when an enabled item is chosen.
  using CommandCallback = base::RepeatingCallback<void(Command command)>;

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
  std::string FolderIdForCommand(Command command) const;

  // The menu items, for tests. Null when the menu does not have the item.
  views::MenuItemView* sleep_item() const;
  views::MenuItemView* item(Command command) const;
  // The layer the open motion plays on; null before Run or when the menu has
  // no layer.
  ui::Layer* open_layer_for_testing() const { return motion_layer_; }

  // The same three, by command.
  bool IsCommandEnabled(Command command) const;
  void ExecuteCommand(Command command);
  std::u16string GetTooltipText(Command command,
                                const gfx::Point& screen_loc) const;

  // views::MenuDelegate (the menu's own int ids are the Command values):
  using views::MenuDelegate::ExecuteCommand;
  bool IsCommandEnabled(int id) const override;
  void ExecuteCommand(int id) override;
  std::u16string GetTooltipText(int id,
                                const gfx::Point& screen_loc) const override;
  void OnMenuClosed(views::MenuItemView* menu) override;

 private:
  bool HasCommand(Command command) const;
  void BuildTabItems();

  Options options_;
  CommandCallback on_command_;
  raw_ptr<views::MenuItemView> root_ = nullptr;
  std::unique_ptr<views::MenuRunner> runner_;
  base::RepeatingClosure closed_;
  aerie::motion::MotionHandle menu_motion_;
  raw_ptr<ui::Layer> motion_layer_ = nullptr;
};



#endif  // CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_CONTEXT_MENU_H_
