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
  using aerie_tab_model::Section;
  auto root = std::make_unique<views::MenuItemView>(this);
  root_ = root.get();
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
  root_->AppendSeparator();
  views::MenuItemView* sleep = root_->AppendMenuItem(
      kCommandSleepTab, l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_SLEEP_TAB));
  sleep->SetEnabled(options_.sleep.enabled);
  root_->AppendMenuItem(kCommandClose,
                        l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_CLOSE_TAB));
  runner_ = std::make_unique<views::MenuRunner>(
      std::move(root), views::MenuRunner::HAS_MNEMONICS);
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

bool AerieSidebarContextMenu::IsCommandEnabled(int id) const {
  // A command that this menu does not have (the Pin item of a Favorite, say)
  // is not enabled.
  if (!root_ || !root_->GetMenuItemByID(id)) {
    return false;
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
  return id == kCommandSleepTab ? options_.sleep.tooltip : std::u16string();
}
