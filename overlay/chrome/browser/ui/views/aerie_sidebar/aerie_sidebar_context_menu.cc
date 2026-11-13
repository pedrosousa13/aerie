// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_context_menu.h"

#include <utility>

#include "chrome/grit/generated_resources.h"
#include "components/aerie_design_tokens/aerie_motion.h"
#include "components/aerie_sidebar/aerie_sidebar_motion.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/gfx/geometry/transform_util.h"
#include "ui/views/controls/menu/menu_item_view.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/controls/menu/submenu_view.h"
#include "ui/views/widget/widget.h"

namespace {
// The menu's own ids are the Command values.
int Id(AerieSidebarContextMenu::Command command) {
  return static_cast<int>(command);
}
}  // namespace

// static
AerieSidebarContextMenu::Command AerieSidebarContextMenu::MoveToFolderCommand(
    size_t index) {
  return static_cast<Command>(Id(Command::kMoveToFolderBase) +
                              static_cast<int>(index));
}

AerieSidebarContextMenu::AerieSidebarContextMenu(Options options,
                                                 CommandCallback on_command)
    : options_(std::move(options)), on_command_(std::move(on_command)) {
  auto root = std::make_unique<views::MenuItemView>(this);
  root_ = root.get();
  auto append_new_folder = [this]() {
    views::MenuItemView* item = root_->AppendMenuItem(
        Id(Command::kNewFolder),
        l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_NEW_FOLDER));
    item->SetEnabled(options_.new_folder.enabled);
  };
  if (options_.target == Target::kSection) {
    append_new_folder();
  } else if (options_.target == Target::kFolder) {
    append_new_folder();
    root_->AppendMenuItem(
        Id(Command::kRenameFolder),
        l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_RENAME_FOLDER));
    root_->AppendMenuItem(
        Id(Command::kEditFolder),
        l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_EDIT_FOLDER));
    root_->AppendSeparator();
    root_->AppendMenuItem(
        Id(Command::kDeleteFolder),
        l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_DELETE_FOLDER));
  } else {
    BuildTabItems();
  }
  runner_ = std::make_unique<views::MenuRunner>(
      std::move(root), views::MenuRunner::HAS_MNEMONICS);
}

void AerieSidebarContextMenu::BuildTabItems() {
  using aerie_tab_model::Section;
  if (options_.away_from_pinned && options_.section == Section::kPinned) {
    root_->AppendMenuItem(
        Id(Command::kBackToPinned),
        l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_BACK_TO_PINNED));
  }
  // The section items. A Favorite goes back to where it was, so it has no Pin
  // item of its own.
  if (options_.section == Section::kPinned) {
    root_->AppendMenuItem(Id(Command::kUnpin),
                          l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_UNPIN_TAB));
  } else if (options_.section == Section::kToday) {
    root_->AppendMenuItem(Id(Command::kPin),
                          l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_PIN_TAB));
  }
  if (options_.section == Section::kFavorite) {
    root_->AppendMenuItem(
        Id(Command::kRemoveFromFavorites),
        l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_REMOVE_FROM_FAVORITES));
  } else {
    root_->AppendMenuItem(
        Id(Command::kAddToFavorites),
        l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_ADD_TO_FAVORITES));
  }
  root_->AppendMenuItem(
      Id(Command::kRename), l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_RENAME_TAB));
  if (options_.has_manual_name) {
    root_->AppendMenuItem(Id(Command::kResetName), l10n_util::GetStringUTF16(
                                                 IDS_AERIE_SIDEBAR_RESET_NAME));
  }
  if (options_.section != Section::kFavorite &&
      (options_.new_folder.shown || options_.move_to_new_folder.shown ||
       !options_.move_targets.empty() || options_.move_out.shown)) {
    root_->AppendSeparator();
    if (options_.new_folder.shown) {
      root_->AppendMenuItem(
          Id(Command::kNewFolder),
          l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_NEW_FOLDER))
          ->SetEnabled(options_.new_folder.enabled);
    }
    if (options_.move_to_new_folder.shown) {
      root_->AppendMenuItem(
              Id(Command::kMoveToNewFolder),
              l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_MOVE_TO_NEW_FOLDER))
          ->SetEnabled(options_.move_to_new_folder.enabled);
    }
    if (!options_.move_targets.empty()) {
      views::MenuItemView* list = root_->AppendSubMenu(
          Id(Command::kMoveToFolderList),
          l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_MOVE_TO_FOLDER));
      for (size_t i = 0; i < options_.move_targets.size(); ++i) {
        list->AppendMenuItem(Id(Command::kMoveToFolderBase) + static_cast<int>(i),
                             options_.move_targets[i].label)
            ->SetEnabled(options_.move_targets[i].enabled);
      }
    }
    if (options_.move_out.shown) {
      root_->AppendMenuItem(
          Id(Command::kMoveOutOfFolder),
          l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_MOVE_OUT_OF_FOLDER));
    }
  }
  if (options_.stop_rule.shown) {
    root_->AppendMenuItem(
        Id(Command::kStopRuleMatching),
        l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_STOP_RULE_MATCHING));
  }
  root_->AppendSeparator();
  views::MenuItemView* sleep = root_->AppendMenuItem(
      Id(Command::kSleepTab), l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_SLEEP_TAB));
  sleep->SetEnabled(options_.sleep.enabled);
  root_->AppendMenuItem(Id(Command::kClose),
                        l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_CLOSE_TAB));
}

AerieSidebarContextMenu::~AerieSidebarContextMenu() {
  menu_motion_.Abort();
  motion_layer_ = nullptr;
}

void AerieSidebarContextMenu::Run(views::Widget* parent,
                                  const gfx::Point& screen_point,
                                  ui::mojom::MenuSourceType source_type,
                                  const PrefService* reduced_motion_prefs) {
  runner_->RunMenuAt(parent, /*button_controller=*/nullptr,
                     gfx::Rect(screen_point, gfx::Size()),
                     views::MenuAnchorPosition::kTopLeft, source_type);
  // The open motion: the menu's own window scales from 0.97 about its top left
  // corner (where it opens from) and fades in. It plays on the layer of the
  // menu's widget; only `transform` and `opacity` change. There is no closing
  // motion.
  views::SubmenuView* submenu = root_->GetSubmenu();
  views::Widget* widget = submenu ? submenu->GetWidget() : nullptr;
  ui::Layer* layer = widget ? widget->GetLayer() : nullptr;
  motion_layer_ = layer;
  if (!layer) {
    return;
  }
  const aerie_sidebar::MenuOpenMotion motion = aerie_sidebar::ContextMenuOpenMotion(
      aerie::motion::ShouldReduceMotion(reduced_motion_prefs));
  layer->SetOpacity(motion.from_opacity);
  aerie::motion::LayerMotion target;
  target.opacity = 1.f;
  if (motion.animate_scale) {
    layer->SetTransform(gfx::Transform::MakeScale(motion.from_scale));
    target.transform = gfx::Transform();
  }
  menu_motion_ = aerie::motion::AnimateLayer(layer, target, motion.duration,
                                             motion.easing,
                                             reduced_motion_prefs);
}

bool AerieSidebarContextMenu::is_running() const {
  return runner_ && runner_->IsRunning();
}

void AerieSidebarContextMenu::Close() {
  menu_motion_.Abort();
  motion_layer_ = nullptr;
  if (runner_) {
    runner_->Cancel();
  }
}

views::MenuItemView* AerieSidebarContextMenu::sleep_item() const {
  return item(Command::kSleepTab);
}

views::MenuItemView* AerieSidebarContextMenu::item(Command command) const {
  return root_->GetMenuItemByID(Id(command));
}

std::string AerieSidebarContextMenu::FolderIdForCommand(Command command) const {
  const int index = Id(command) - Id(Command::kMoveToFolderBase);
  if (index < 0 || index >= static_cast<int>(options_.move_targets.size())) {
    return std::string();
  }
  return options_.move_targets[index].folder_id;
}

bool AerieSidebarContextMenu::HasCommand(Command command) const {
  using aerie_tab_model::Section;
  if (Id(command) >= Id(Command::kMoveToFolderBase)) {
    return options_.target == Target::kTab &&
           Id(command) - Id(Command::kMoveToFolderBase) <
               static_cast<int>(options_.move_targets.size());
  }
  switch (command) {
    case Command::kNewFolder:
      return options_.target != Target::kTab || options_.new_folder.shown;
    case Command::kRenameFolder:
    case Command::kEditFolder:
    case Command::kDeleteFolder:
      return options_.target == Target::kFolder;
    case Command::kStopRuleMatching:
      return options_.target == Target::kTab && options_.stop_rule.shown;
    case Command::kMoveToNewFolder:
      return options_.target == Target::kTab &&
             options_.move_to_new_folder.shown;
    case Command::kMoveOutOfFolder:
      return options_.target == Target::kTab && options_.move_out.shown;
    case Command::kMoveToFolderList:
      // The parent of the list: it has to be enabled for the list to open.
      return options_.target == Target::kTab && !options_.move_targets.empty();
    case Command::kSleepTab:
    case Command::kClose:
    case Command::kRename:
      return options_.target == Target::kTab;
    case Command::kResetName:
      return options_.has_manual_name;
    case Command::kPin:
      return options_.section == Section::kToday;
    case Command::kUnpin:
      return options_.section == Section::kPinned;
    case Command::kAddToFavorites:
      return options_.section != Section::kFavorite;
    case Command::kRemoveFromFavorites:
      return options_.section == Section::kFavorite;
    case Command::kBackToPinned:
      return options_.away_from_pinned && options_.section == Section::kPinned;
    case Command::kMoveToFolderBase:
      break;
  }
  return false;
}

bool AerieSidebarContextMenu::IsCommandEnabled(Command command) const {
  // A command that this menu does not have (the Pin item of a Favorite, say)
  // is not enabled. This is read from the options, not from the item tree: the
  // menu asks while it is still adding the item.
  if (!HasCommand(command)) {
    return false;
  }
  if (command == Command::kNewFolder) {
    return options_.new_folder.enabled;
  }
  if (command == Command::kMoveToNewFolder) {
    return options_.move_to_new_folder.enabled;
  }
  if (Id(command) >= Id(Command::kMoveToFolderBase)) {
    return options_.move_targets[Id(command) - Id(Command::kMoveToFolderBase)]
        .enabled;
  }
  return command != Command::kSleepTab || options_.sleep.enabled;
}

void AerieSidebarContextMenu::ExecuteCommand(Command command) {
  if (IsCommandEnabled(command) && on_command_) {
    on_command_.Run(command);
  }
}

void AerieSidebarContextMenu::OnMenuClosed(views::MenuItemView* menu) {
  // The menu's window and layer go away with the menu: the open motion must not
  // outlive them.
  menu_motion_.Abort();
  motion_layer_ = nullptr;
}

std::u16string AerieSidebarContextMenu::GetTooltipText(
    Command command,
    const gfx::Point& screen_loc) const {
  if (command == Command::kNewFolder) {
    return options_.new_folder.enabled ? std::u16string()
                                       : options_.new_folder.tooltip;
  }
  if (command == Command::kMoveToNewFolder) {
    return options_.move_to_new_folder.enabled
               ? std::u16string()
               : options_.move_to_new_folder.tooltip;
  }
  return command == Command::kSleepTab ? options_.sleep.tooltip
                                       : std::u16string();
}

bool AerieSidebarContextMenu::IsCommandEnabled(int id) const {
  return IsCommandEnabled(static_cast<Command>(id));
}

void AerieSidebarContextMenu::ExecuteCommand(int id) {
  ExecuteCommand(static_cast<Command>(id));
}

std::u16string AerieSidebarContextMenu::GetTooltipText(
    int id,
    const gfx::Point& screen_loc) const {
  return GetTooltipText(static_cast<Command>(id), screen_loc);
}
