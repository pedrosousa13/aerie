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

AerieSidebarContextMenu::AerieSidebarContextMenu(Options options,
                                                 CommandCallback on_command)
    : options_(std::move(options)), on_command_(std::move(on_command)) {
  auto root = std::make_unique<views::MenuItemView>(this);
  root_ = root.get();
  auto append_new_folder = [this]() {
    views::MenuItemView* item = root_->AppendMenuItem(
        kCommandNewFolder,
        l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_NEW_FOLDER));
    item->SetEnabled(options_.new_folder_enabled);
  };
  if (options_.target == Target::kSection) {
    append_new_folder();
  } else if (options_.target == Target::kFolder) {
    append_new_folder();
    root_->AppendMenuItem(
        kCommandRenameFolder,
        l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_RENAME_FOLDER));
    root_->AppendSeparator();
    root_->AppendMenuItem(
        kCommandDeleteFolder,
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
        kCommandBackToPinned,
        l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_BACK_TO_PINNED));
  }
  // The section items. A Favorite goes back to where it was, so it has no Pin
  // item of its own.
  if (options_.section == Section::kPinned) {
    root_->AppendMenuItem(kCommandUnpin,
                          l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_UNPIN_TAB));
  } else if (options_.section == Section::kToday) {
    root_->AppendMenuItem(kCommandPin,
                          l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_PIN_TAB));
  }
  if (options_.section == Section::kFavorite) {
    root_->AppendMenuItem(
        kCommandRemoveFromFavorites,
        l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_REMOVE_FROM_FAVORITES));
  } else {
    root_->AppendMenuItem(
        kCommandAddToFavorites,
        l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_ADD_TO_FAVORITES));
  }
  root_->AppendMenuItem(
      kCommandRename, l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_RENAME_TAB));
  if (options_.has_manual_name) {
    root_->AppendMenuItem(kCommandResetName, l10n_util::GetStringUTF16(
                                                 IDS_AERIE_SIDEBAR_RESET_NAME));
  }
  if (options_.section != Section::kFavorite &&
      (options_.show_new_folder || options_.show_move_to_new_folder ||
       !options_.move_targets.empty() || options_.show_move_out)) {
    root_->AppendSeparator();
    if (options_.show_new_folder) {
      root_->AppendMenuItem(
          kCommandNewFolder,
          l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_NEW_FOLDER))
          ->SetEnabled(options_.new_folder_enabled);
    }
    if (options_.show_move_to_new_folder) {
      root_->AppendMenuItem(
              kCommandMoveToNewFolder,
              l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_MOVE_TO_NEW_FOLDER))
          ->SetEnabled(options_.move_to_new_folder_enabled);
    }
    if (!options_.move_targets.empty()) {
      views::MenuItemView* list = root_->AppendSubMenu(
          kCommandMoveToFolderList,
          l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_MOVE_TO_FOLDER));
      for (size_t i = 0; i < options_.move_targets.size(); ++i) {
        list->AppendMenuItem(kCommandMoveToFolderBase + static_cast<int>(i),
                             options_.move_targets[i].label)
            ->SetEnabled(options_.move_targets[i].enabled);
      }
    }
    if (options_.show_move_out) {
      root_->AppendMenuItem(
          kCommandMoveOutOfFolder,
          l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_MOVE_OUT_OF_FOLDER));
    }
  }
  root_->AppendSeparator();
  views::MenuItemView* sleep = root_->AppendMenuItem(
      kCommandSleepTab, l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_SLEEP_TAB));
  sleep->SetEnabled(options_.sleep.enabled);
  root_->AppendMenuItem(kCommandClose,
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
  return item(kCommandSleepTab);
}

views::MenuItemView* AerieSidebarContextMenu::item(int command) const {
  return root_->GetMenuItemByID(command);
}

std::string AerieSidebarContextMenu::FolderIdForCommand(int command) const {
  const int index = command - kCommandMoveToFolderBase;
  if (index < 0 || index >= static_cast<int>(options_.move_targets.size())) {
    return std::string();
  }
  return options_.move_targets[index].folder_id;
}

bool AerieSidebarContextMenu::HasCommand(int id) const {
  using aerie_tab_model::Section;
  if (id >= kCommandMoveToFolderBase) {
    return options_.target == Target::kTab &&
           id - kCommandMoveToFolderBase <
               static_cast<int>(options_.move_targets.size());
  }
  switch (id) {
    case kCommandNewFolder:
      return options_.target != Target::kTab || options_.show_new_folder;
    case kCommandRenameFolder:
    case kCommandDeleteFolder:
      return options_.target == Target::kFolder;
    case kCommandMoveToNewFolder:
      return options_.target == Target::kTab && options_.show_move_to_new_folder;
    case kCommandMoveOutOfFolder:
      return options_.target == Target::kTab && options_.show_move_out;
    case kCommandMoveToFolderList:
      // The parent of the list: it has to be enabled for the list to open.
      return options_.target == Target::kTab && !options_.move_targets.empty();
    case kCommandSleepTab:
    case kCommandClose:
    case kCommandRename:
      return options_.target == Target::kTab;
    case kCommandResetName:
      return options_.has_manual_name;
    case kCommandPin:
      return options_.section == Section::kToday;
    case kCommandUnpin:
      return options_.section == Section::kPinned;
    case kCommandAddToFavorites:
      return options_.section != Section::kFavorite;
    case kCommandRemoveFromFavorites:
      return options_.section == Section::kFavorite;
    case kCommandBackToPinned:
      return options_.away_from_pinned && options_.section == Section::kPinned;
  }
  return false;
}

bool AerieSidebarContextMenu::IsCommandEnabled(int id) const {
  // A command that this menu does not have (the Pin item of a Favorite, say)
  // is not enabled. This is read from the options, not from the item tree: the
  // menu asks while it is still adding the item.
  if (!HasCommand(id)) {
    return false;
  }
  if (id == kCommandNewFolder) {
    return options_.new_folder_enabled;
  }
  if (id == kCommandMoveToNewFolder) {
    return options_.move_to_new_folder_enabled;
  }
  if (id >= kCommandMoveToFolderBase) {
    return options_.move_targets[id - kCommandMoveToFolderBase].enabled;
  }
  return id != kCommandSleepTab || options_.sleep.enabled;
}

void AerieSidebarContextMenu::ExecuteCommand(int id) {
  if (IsCommandEnabled(id) && on_command_) {
    on_command_.Run(id);
  }
}

void AerieSidebarContextMenu::OnMenuClosed(views::MenuItemView* menu) {
  // The menu's window and layer go away with the menu: the open motion must not
  // outlive them.
  menu_motion_.Abort();
  motion_layer_ = nullptr;
}

std::u16string AerieSidebarContextMenu::GetTooltipText(
    int id,
    const gfx::Point& screen_loc) const {
  if (id == kCommandNewFolder) {
    return options_.new_folder_enabled ? std::u16string()
                                       : options_.new_folder_tooltip;
  }
  if (id == kCommandMoveToNewFolder) {
    return options_.move_to_new_folder_enabled
               ? std::u16string()
               : options_.move_to_new_folder_tooltip;
  }
  return id == kCommandSleepTab ? options_.sleep.tooltip : std::u16string();
}
