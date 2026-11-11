// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_CONTEXT_MENU_H_
#define CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_CONTEXT_MENU_H_

#include <memory>
#include <string>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "ui/base/mojom/menu_source_type.mojom-forward.h"
#include "ui/gfx/geometry/point.h"
#include "ui/views/controls/menu/menu_delegate.h"

namespace views {
class MenuItemView;
class MenuRunner;
class Widget;
}  // namespace views

// The context menu of a tab row of the sidebar (issue #179). Today it has one
// item, "Sleep tab"; the full menu is the one of issue #180. The item is
// disabled, with the reason in its tooltip, when the tab cannot sleep. The menu
// is the views menu, so the keyboard works as in every menu: arrows, Enter and
// Esc.
class AerieSidebarContextMenu : public views::MenuDelegate {
 public:
  static constexpr int kCommandSleepTab = 1;

  struct SleepItem {
    bool enabled = false;
    // Why the item is disabled; empty when it is enabled.
    std::u16string tooltip;
  };

  // `on_sleep` runs when the item is chosen.
  AerieSidebarContextMenu(SleepItem sleep, base::OnceClosure on_sleep);
  AerieSidebarContextMenu(const AerieSidebarContextMenu&) = delete;
  AerieSidebarContextMenu& operator=(const AerieSidebarContextMenu&) = delete;
  ~AerieSidebarContextMenu() override;

  // Opens the menu at `screen_point`.
  void Run(views::Widget* parent,
           const gfx::Point& screen_point,
           ui::mojom::MenuSourceType source_type);
  bool is_running() const;
  // Closes the menu without choosing.
  void Close();

  // The menu items, for tests.
  views::MenuItemView* sleep_item() const;

  // views::MenuDelegate:
  bool IsCommandEnabled(int id) const override;
  void ExecuteCommand(int id) override;
  std::u16string GetTooltipText(int id,
                                const gfx::Point& screen_loc) const override;

 private:
  SleepItem sleep_;
  base::OnceClosure on_sleep_;
  raw_ptr<views::MenuItemView> root_ = nullptr;
  std::unique_ptr<views::MenuRunner> runner_;
};

#endif  // CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_CONTEXT_MENU_H_
