// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/ui/views/aerie_adblock/aerie_guard_page_action_controller.h"

#include <string>
#include <utility>

#include "chrome/browser/ui/actions/chrome_action_id.h"
#include "chrome/browser/ui/browser_actions.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/page_action/page_action_controller.h"
#include "chrome/browser/ui/views/aerie_adblock/aerie_guard_image.h"
#include "chrome/browser/ui/views/aerie_adblock/vector_icons/vector_icons.h"
#include "chrome/browser/ui/webui/aerie_guard/aerie_guard_panel_target.h"
#include "chrome/browser/ui/webui/aerie_guard/aerie_guard_ui.h"
#include "chrome/grit/generated_resources.h"
#include "chrome/common/webui_url_constants.h"
#include "components/aerie_adblock/browser/guard_target.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/color/color_id.h"
#include "ui/views/view.h"
#include "url/gurl.h"

AerieGuardPageActionController::AerieGuardPageActionController(
    tabs::TabInterface& tab_interface,
    page_actions::PageActionController& page_action_controller)
    : tabs::ContentsObservingTabFeature(tab_interface),
      page_action_controller_(page_action_controller) {
  if (aerie_adblock::FilterListManager* manager =
          aerie_adblock::FilterListManager::Get()) {
    manager_observation_.Observe(manager);
  }
  ObserveCounterOf(web_contents());
  Update();
}

AerieGuardPageActionController::~AerieGuardPageActionController() {
  // The tab is going away: the panel that acts on it goes too.
  widget_observation_.Reset();
  bubble_manager_.reset();
  BubbleClosed();
  ObserveCounterOf(nullptr);
}

// static
ui::ImageModel AerieGuardPageActionController::DefaultImage() {
  return ui::ImageModel::FromVectorIcon(aerie_guard::kAerieGuardOnIcon,
                                        ui::kColorIcon,
                                        aerie_guard::kGuardImageSize);
}

aerie_adblock::GuardState AerieGuardPageActionController::CurrentState() const {
  return aerie_adblock::GuardStateOfTab(web_contents());
}

void AerieGuardPageActionController::ObserveCounterOf(
    content::WebContents* contents) {
  if (contents == counted_contents_.get()) {
    return;
  }
  if (counted_contents_) {
    aerie_adblock::PageBlockCounter::RemoveObserverFor(counted_contents_.get(),
                                                       this);
  }
  counted_contents_ = contents ? contents->GetWeakPtr() : nullptr;
  if (counted_contents_) {
    aerie_adblock::PageBlockCounter::AddObserverFor(counted_contents_.get(),
                                                    this);
  }
}

void AerieGuardPageActionController::Update() {
  const aerie_adblock::GuardState state = CurrentState();
  if (!state.visible) {
    page_action_controller_->Hide(kActionAerieGuard);
    return;
  }
  std::u16string tooltip;
  if (state.blocking_off) {
    tooltip = l10n_util::GetStringUTF16(IDS_AERIE_GUARD_TOOLTIP_OFF);
  } else if (state.count > 0) {
    tooltip = l10n_util::GetPluralStringFUTF16(IDS_AERIE_GUARD_TOOLTIP_BLOCKED,
                                               state.count);
  } else {
    tooltip = l10n_util::GetStringUTF16(IDS_AERIE_GUARD_TOOLTIP_NONE);
  }
  // The label is empty while blocking is off: the count is 0 then.
  page_action_controller_->OverrideImage(
      kActionAerieGuard,
      aerie_guard::GuardImage(state.blocking_off,
                              aerie_adblock::GuardBadgeLabel(state.count)));
  page_action_controller_->OverrideTooltip(kActionAerieGuard, tooltip);
  page_action_controller_->OverrideAccessibleName(kActionAerieGuard, tooltip);
  page_action_controller_->Show(kActionAerieGuard);
}

void AerieGuardPageActionController::PrimaryPageChanged(content::Page& page) {
  Update();
}

void AerieGuardPageActionController::OnDiscardContents(
    tabs::TabInterface* tab,
    content::WebContents* old_contents,
    content::WebContents* new_contents) {
  tabs::ContentsObservingTabFeature::OnDiscardContents(tab, old_contents,
                                                       new_contents);
  ObserveCounterOf(new_contents);
  Update();
}

void AerieGuardPageActionController::OnPageBlockedCountChanged(
    content::WebContents* web_contents,
    int count) {
  if (web_contents == counted_contents_.get()) {
    Update();
  }
}

void AerieGuardPageActionController::OnFilterListsChanged() {
  Update();
}

void AerieGuardPageActionController::ShowPanel(views::BubbleAnchor anchor) {
  views::View* anchor_view = anchor.GetIfView();
  if (!anchor_view || !anchor_view->GetWidget() || !CurrentState().visible) {
    return;
  }
  if (bubble_manager_ && bubble_manager_->GetBubbleWidget()) {
    bubble_manager_->CloseBubble();
    return;
  }
  BrowserWindowInterface* browser = tab().GetBrowserWindowInterface();
  action_item_ = actions::ActionManager::Get()
                     .FindAction(kActionAerieGuard,
                                 BrowserActions::From(browser)->root_action_item())
                     ->GetAsWeakPtr();
  bubble_manager_ = WebUIBubbleManager::Create<aerie_guard::AerieGuardUI>(
      browser, GURL(chrome::kChromeUIAerieGuardURL), IDS_AERIE_GUARD_PANEL_ACCNAME);
  bubble_manager_observation_.Reset();
  bubble_manager_observation_.Observe(bubble_manager_.get());
  bubble_manager_->ShowBubble(anchor_view, views::BubbleBorder::TOP_RIGHT);
  // The panel's page asks for this once it loads, after this call returns. It
  // is the browser's own capture of the tab and site shown now.
  aerie_guard::AerieGuardPanelTarget::Set(
      bubble_manager_->GetContentsWrapper()->web_contents(),
      aerie_adblock::GuardTarget::Capture(web_contents()));
}

void AerieGuardPageActionController::BeforeBubbleWidgetShowed(
    views::Widget* widget) {
  widget_observation_.Reset();
  widget_observation_.Observe(widget);
  if (action_item_) {
    action_item_->SetIsShowingBubble(true);
  }
}

void AerieGuardPageActionController::OnWidgetDestroying(views::Widget* widget) {
  widget_observation_.Reset();
  BubbleClosed();
}

void AerieGuardPageActionController::BubbleClosed() {
  if (action_item_) {
    action_item_->SetIsShowingBubble(false);
  }
}
