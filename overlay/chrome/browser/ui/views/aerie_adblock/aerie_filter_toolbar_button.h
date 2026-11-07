// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_AERIE_ADBLOCK_AERIE_FILTER_TOOLBAR_BUTTON_H_
#define CHROME_BROWSER_UI_VIEWS_AERIE_ADBLOCK_AERIE_FILTER_TOOLBAR_BUTTON_H_

#include <memory>
#include <string>

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "chrome/browser/ui/views/toolbar/toolbar_button.h"
#include "components/aerie_adblock/browser/filter_list_manager.h"
#include "components/aerie_adblock/browser/page_block_counter.h"
#include "content/public/browser/web_contents.h"
#include "ui/menus/simple_menu_model.h"

class BrowserWindowInterface;

namespace views {
class MenuRunner;
}

namespace aerie_adblock {

// The toolbar button of the Filter Engine (issue #10): a shield with the number
// of requests blocked on the page of the active tab (nothing while it is 0).
// Pressing it opens a menu to turn blocking off, or on again, for the page's
// site, and to open chrome://aerie-filters. The sidebar and the command bar
// get this later (M2, M3).
//
// Added to the toolbar by patches/adblock/0010.
class AerieFilterToolbarButton : public ToolbarButton,
                                 public PageBlockCounter::Observer,
                                 public FilterListManager::Observer,
                                 public ui::SimpleMenuModel::Delegate {
  METADATA_HEADER(AerieFilterToolbarButton, ToolbarButton)

 public:
  explicit AerieFilterToolbarButton(BrowserWindowInterface* browser);
  AerieFilterToolbarButton(const AerieFilterToolbarButton&) = delete;
  AerieFilterToolbarButton& operator=(const AerieFilterToolbarButton&) = delete;
  ~AerieFilterToolbarButton() override;

  // The text on the button for |count| blocked requests: "" for 0, the number
  // up to 99, "99+" above. Public for tests.
  static std::u16string LabelForCount(int count);

  // PageBlockCounter::Observer:
  void OnPageBlockedCountChanged(content::WebContents* web_contents,
                                 int count) override;
  // FilterListManager::Observer:
  void OnFilterListsChanged() override;

  // ui::SimpleMenuModel::Delegate:
  void ExecuteCommand(int command_id, int event_flags) override;

 private:
  enum Command { kToggleSite = 1, kManageLists = 2 };

  void ButtonPressed();
  void OnActiveTabChanged();
  void Update();
  // The host of the active page when it is an http(s) page, else "".
  std::string ActiveHost() const;
  content::WebContents* ActiveWebContents() const;

  const raw_ptr<BrowserWindowInterface> browser_;
  raw_ptr<content::WebContents> observed_ = nullptr;
  base::CallbackListSubscription active_tab_subscription_;
  base::ScopedObservation<FilterListManager, FilterListManager::Observer>
      manager_observation_{this};
  std::unique_ptr<ui::SimpleMenuModel> menu_model_;
  std::unique_ptr<views::MenuRunner> menu_runner_;
};

}  // namespace aerie_adblock

#endif  // CHROME_BROWSER_UI_VIEWS_AERIE_ADBLOCK_AERIE_FILTER_TOOLBAR_BUTTON_H_
