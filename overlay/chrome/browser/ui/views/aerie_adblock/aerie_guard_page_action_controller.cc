// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/ui/views/aerie_adblock/aerie_guard_page_action_controller.h"

#include <string>
#include <utility>

#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ui/actions/chrome_action_id.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/page_action/page_action_controller.h"
#include "chrome/browser/ui/views/aerie_adblock/aerie_guard_image.h"
#include "chrome/browser/ui/views/aerie_adblock/vector_icons/vector_icons.h"
#include "chrome/grit/generated_resources.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/mojom/menu_source_type.mojom.h"
#include "ui/base/window_open_disposition.h"
#include "ui/color/color_id.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace {

constexpr char kManagerPageUrl[] = "chrome://filters/";
// The size of the icon of a page action, in dip.
constexpr int kIconSize = 16;

}  // namespace

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
  ObserveCounterOf(nullptr);
}

// static
ui::ImageModel AerieGuardPageActionController::DefaultImage() {
  return ui::ImageModel::FromVectorIcon(aerie_guard::kAerieGuardOnIcon,
                                        ui::kColorIcon, kIconSize);
}

aerie_adblock::GuardState AerieGuardPageActionController::CurrentState() const {
  content::WebContents* contents = web_contents();
  if (!contents) {
    return aerie_adblock::GuardState();
  }
  // The committed URL of the primary main frame: what the browser process
  // knows, never a URL a renderer proposes.
  const GURL& url = contents->GetPrimaryMainFrame()->GetLastCommittedURL();
  return aerie_adblock::ComputeGuardState(
      url, aerie_adblock::SessionKeyFor(contents->GetBrowserContext()),
      aerie_adblock::FilterListManager::Get(),
      aerie_adblock::PageBlockCounter::CountFor(contents));
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

void AerieGuardPageActionController::ShowMenu(views::BubbleAnchor anchor) {
  views::View* anchor_view = anchor.GetIfView();
  const aerie_adblock::GuardState state = CurrentState();
  if (!anchor_view || !anchor_view->GetWidget() || !state.visible) {
    return;
  }
  const bool incognito = aerie_adblock::SessionKeyFor(
                             web_contents()->GetBrowserContext())
                             .is_off_the_record();
  const std::u16string host = base::UTF8ToUTF16(state.host);
  menu_host_ = state.host;
  menu_model_ = std::make_unique<ui::SimpleMenuModel>(this);
  int toggle_id;
  if (state.locked) {
    // An exception saved in a regular window cannot be undone from incognito.
    toggle_id = IDS_AERIE_GUARD_MENU_LOCKED;
  } else if (state.blocking_off) {
    toggle_id = incognito ? IDS_AERIE_GUARD_MENU_TURN_ON_INCOGNITO
                          : IDS_AERIE_GUARD_MENU_TURN_ON;
  } else {
    toggle_id = incognito ? IDS_AERIE_GUARD_MENU_TURN_OFF_INCOGNITO
                          : IDS_AERIE_GUARD_MENU_TURN_OFF;
  }
  menu_model_->AddItem(kToggleSite,
                       state.locked ? l10n_util::GetStringUTF16(toggle_id)
                                    : l10n_util::GetStringFUTF16(toggle_id, host));
  menu_model_->SetEnabledAt(menu_model_->GetItemCount() - 1, !state.locked);
  menu_model_->AddItem(
      kManageLists, l10n_util::GetStringUTF16(IDS_AERIE_GUARD_MENU_MANAGE_LISTS));
  menu_runner_ = std::make_unique<views::MenuRunner>(
      menu_model_.get(), views::MenuRunner::HAS_MNEMONICS);
  menu_runner_->RunMenuAt(anchor_view->GetWidget(), nullptr,
                          anchor.GetAnchorRect(),
                          views::MenuAnchorPosition::kTopRight,
                          ui::mojom::MenuSourceType::kMouse);
}

void AerieGuardPageActionController::ExecuteCommand(int command_id,
                                                    int event_flags) {
  switch (command_id) {
    case kToggleSite: {
      aerie_adblock::FilterListManager* manager =
          aerie_adblock::FilterListManager::Get();
      content::WebContents* contents = web_contents();
      const aerie_adblock::GuardState state = CurrentState();
      // Act only on the site the menu was built for, and never on one an
      // incognito window cannot change.
      if (!manager || !contents || !state.visible || state.locked ||
          state.host != menu_host_) {
        return;
      }
      // The manager watches the session and refuses to undo a saved exception
      // from incognito. An incognito toggle is memory only.
      if (manager->SetException(state.host, !state.blocking_off,
                                contents->GetBrowserContext()) ==
          aerie_adblock::ExceptionResult::kOk) {
        // An excepted site loads its ads on the next load; the page that is
        // open keeps what it has. Reload so the change is visible at once.
        contents->GetController().Reload(content::ReloadType::NORMAL, true);
      }
      break;
    }
    case kManageLists:
      tab().GetBrowserWindowInterface()->OpenGURL(
          GURL(kManagerPageUrl), WindowOpenDisposition::NEW_FOREGROUND_TAB);
      break;
  }
}
