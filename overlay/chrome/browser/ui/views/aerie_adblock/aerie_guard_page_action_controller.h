// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_AERIE_ADBLOCK_AERIE_GUARD_PAGE_ACTION_CONTROLLER_H_
#define CHROME_BROWSER_UI_VIEWS_AERIE_ADBLOCK_AERIE_GUARD_PAGE_ACTION_CONTROLLER_H_

#include <memory>
#include <string>

#include "base/memory/raw_ref.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "chrome/browser/ui/tabs/contents_observing_tab_feature.h"
#include "components/aerie_adblock/browser/filter_list_manager.h"
#include "components/aerie_adblock/browser/guard_state.h"
#include "components/aerie_adblock/browser/page_block_counter.h"
#include "ui/base/models/image_model.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/bubble/bubble_anchor.h"

namespace page_actions {
class PageActionController;
}  // namespace page_actions

namespace tabs {
class TabInterface;
}  // namespace tabs

namespace views {
class MenuRunner;
}  // namespace views

// The Aerie Guard icon of one tab (issue #99): the address bar icon. It
// shows on http and https pages only. The icon is the shield "on" or "off for
// this site", with the number of requests blocked on the page drawn into it.
// Its tooltip and accessible name say the same in words.
//
// Everything the icon shows comes from browser-side state: the committed URL of
// the tab's primary main frame, the FilterListManager's exceptions and the
// PageBlockCounter's count (see aerie_adblock::ComputeGuardState()).
//
// Pressing the icon opens a menu to turn Guard off, or on again, for the site
// (then the tab reloads) and to open chrome://filters. The pop-out panel
// replaces the menu in issue #100.
//
// Created for every tab by TabFeatures (patches/adblock/0016), modelled on
// JsOptimizationsPageActionController.
class AerieGuardPageActionController
    : public tabs::ContentsObservingTabFeature,
      public aerie_adblock::PageBlockCounter::Observer,
      public aerie_adblock::FilterListManager::Observer,
      public ui::SimpleMenuModel::Delegate {
 public:
  AerieGuardPageActionController(
      tabs::TabInterface& tab_interface,
      page_actions::PageActionController& page_action_controller);
  AerieGuardPageActionController(const AerieGuardPageActionController&) =
      delete;
  AerieGuardPageActionController& operator=(
      const AerieGuardPageActionController&) = delete;
  ~AerieGuardPageActionController() override;

  // The image of the action item before any tab overrides it.
  static ui::ImageModel DefaultImage();

  // Opens the menu at |anchor| (the icon). Does nothing for a page the icon
  // does not show on.
  void ShowMenu(views::BubbleAnchor anchor);

  // content::WebContentsObserver:
  void PrimaryPageChanged(content::Page& page) override;

  // aerie_adblock::PageBlockCounter::Observer:
  void OnPageBlockedCountChanged(content::WebContents* web_contents,
                                 int count) override;
  // aerie_adblock::FilterListManager::Observer:
  void OnFilterListsChanged() override;

  // ui::SimpleMenuModel::Delegate:
  void ExecuteCommand(int command_id, int event_flags) override;

 protected:
  // tabs::ContentsObservingTabFeature:
  void OnDiscardContents(tabs::TabInterface* tab,
                         content::WebContents* old_contents,
                         content::WebContents* new_contents) override;

 private:
  enum Command { kToggleSite = 1, kManageLists = 2 };

  // The state of the icon right now.
  aerie_adblock::GuardState CurrentState() const;
  // Starts counting for |contents| (and stops for the one before).
  void ObserveCounterOf(content::WebContents* contents);
  // Shows, hides or redraws the icon for the current state.
  void Update();

  const raw_ref<page_actions::PageActionController> page_action_controller_;
  base::WeakPtr<content::WebContents> counted_contents_;
  base::ScopedObservation<aerie_adblock::FilterListManager,
                          aerie_adblock::FilterListManager::Observer>
      manager_observation_{this};
  // The host the open menu was built for. A press on the menu acts on it only
  // while the tab still shows it.
  std::string menu_host_;
  std::unique_ptr<ui::SimpleMenuModel> menu_model_;
  std::unique_ptr<views::MenuRunner> menu_runner_;
};

#endif  // CHROME_BROWSER_UI_VIEWS_AERIE_ADBLOCK_AERIE_GUARD_PAGE_ACTION_CONTROLLER_H_
