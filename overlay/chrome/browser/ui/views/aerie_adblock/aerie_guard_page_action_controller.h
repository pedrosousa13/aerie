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
#include "chrome/browser/ui/views/bubble/webui_bubble_manager.h"
#include "chrome/browser/ui/views/bubble/webui_bubble_manager_observer.h"
#include "components/aerie_adblock/browser/filter_list_manager.h"
#include "components/aerie_adblock/browser/guard_state.h"
#include "components/aerie_adblock/browser/page_block_counter.h"
#include "ui/actions/actions.h"
#include "ui/base/models/image_model.h"
#include "ui/views/bubble/bubble_anchor.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_observer.h"

namespace page_actions {
class PageActionController;
}  // namespace page_actions

namespace tabs {
class TabInterface;
}  // namespace tabs

// The Aerie Guard icon of one tab (issue #99): the address bar icon. It
// shows on http and https pages only. The icon is the shield "on" or "off for
// this site", with the number of requests blocked on the page drawn into it.
// Its tooltip and accessible name say the same in words.
//
// Everything the icon shows comes from browser-side state: the committed URL of
// the tab's primary main frame, the FilterListManager's exceptions and the
// PageBlockCounter's count (see aerie_adblock::ComputeGuardState()).
//
// Pressing the icon opens the Guard panel (issue #100): a bubble that hosts
// chrome://aerie-guard.top-chrome, anchored to the icon. The panel acts on the
// tab and site this controller captures when it opens it. Pressing the icon
// again closes the panel: while it is open the action item says it shows a
// bubble, so the click that deactivates the bubble does not open it again
// (the pattern of MemorySaverBubbleController).
//
// Created for every tab by TabFeatures (patches/adblock/0016), modelled on
// JsOptimizationsPageActionController.
class AerieGuardPageActionController
    : public tabs::ContentsObservingTabFeature,
      public aerie_adblock::PageBlockCounter::Observer,
      public aerie_adblock::FilterListManager::Observer,
      public WebUIBubbleManagerObserver,
      public views::WidgetObserver {
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

  // Opens the panel at |anchor| (the icon), or closes it if it is open. Does
  // nothing for a page the icon does not show on.
  void ShowPanel(views::BubbleAnchor anchor);

  // content::WebContentsObserver:
  void PrimaryPageChanged(content::Page& page) override;

  // aerie_adblock::PageBlockCounter::Observer:
  void OnPageBlockedCountChanged(content::WebContents* web_contents,
                                 int count) override;
  // aerie_adblock::FilterListManager::Observer:
  void OnFilterListsChanged() override;

  // WebUIBubbleManagerObserver:
  void BeforeBubbleWidgetShowed(views::Widget* widget) override;

  // views::WidgetObserver:
  void OnWidgetDestroying(views::Widget* widget) override;

 protected:
  // tabs::ContentsObservingTabFeature:
  void OnDiscardContents(tabs::TabInterface* tab,
                         content::WebContents* old_contents,
                         content::WebContents* new_contents) override;

 private:
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
  // Tells the action item that no bubble shows any more.
  void BubbleClosed();

  // A new manager for every press, so every panel is a new page that gets its
  // own captured target: no page outlives the site it was opened for.
  std::unique_ptr<WebUIBubbleManager> bubble_manager_;
  base::ScopedObservation<WebUIBubbleManager, WebUIBubbleManagerObserver>
      bubble_manager_observation_{this};
  base::ScopedObservation<views::Widget, views::WidgetObserver>
      widget_observation_{this};
  base::WeakPtr<actions::ActionItem> action_item_;
};

#endif  // CHROME_BROWSER_UI_VIEWS_AERIE_ADBLOCK_AERIE_GUARD_PAGE_ACTION_CONTROLLER_H_
