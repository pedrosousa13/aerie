// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_context_menu.h"

#include <utility>

#include "chrome/grit/generated_resources.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/controls/menu/menu_item_view.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/widget/widget.h"

AerieSidebarContextMenu::AerieSidebarContextMenu(SleepItem sleep,
                                                 base::OnceClosure on_sleep)
    : sleep_(std::move(sleep)), on_sleep_(std::move(on_sleep)) {
  auto root = std::make_unique<views::MenuItemView>(this);
  root_ = root.get();
  views::MenuItemView* item = root_->AppendMenuItem(
      kCommandSleepTab, l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_SLEEP_TAB));
  item->SetEnabled(sleep_.enabled);
  runner_ = std::make_unique<views::MenuRunner>(
      std::move(root), views::MenuRunner::HAS_MNEMONICS);
}

AerieSidebarContextMenu::~AerieSidebarContextMenu() = default;

void AerieSidebarContextMenu::Run(views::Widget* parent,
                                  const gfx::Point& screen_point,
                                  ui::mojom::MenuSourceType source_type) {
  runner_->RunMenuAt(parent, /*button_controller=*/nullptr,
                     gfx::Rect(screen_point, gfx::Size()),
                     views::MenuAnchorPosition::kTopLeft, source_type);
}

bool AerieSidebarContextMenu::is_running() const {
  return runner_ && runner_->IsRunning();
}

void AerieSidebarContextMenu::Close() {
  if (runner_) {
    runner_->Cancel();
  }
}

views::MenuItemView* AerieSidebarContextMenu::sleep_item() const {
  return root_->GetMenuItemByID(kCommandSleepTab);
}

bool AerieSidebarContextMenu::IsCommandEnabled(int id) const {
  return id == kCommandSleepTab && sleep_.enabled;
}

void AerieSidebarContextMenu::ExecuteCommand(int id) {
  if (id == kCommandSleepTab && sleep_.enabled && on_sleep_) {
    std::move(on_sleep_).Run();
  }
}

std::u16string AerieSidebarContextMenu::GetTooltipText(
    int id,
    const gfx::Point& screen_loc) const {
  return id == kCommandSleepTab ? sleep_.tooltip : std::u16string();
}
