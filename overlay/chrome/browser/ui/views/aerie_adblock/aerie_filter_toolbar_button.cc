// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/ui/views/aerie_adblock/aerie_filter_toolbar_button.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "components/tabs/public/tab_interface.h"
#include "components/vector_icons/vector_icons.h"
#include "ui/base/mojom/menu_source_type.mojom.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace aerie_adblock {

namespace {
constexpr char kManagerPageUrl[] = "chrome://aerie-filters/";
}  // namespace

AerieFilterToolbarButton::AerieFilterToolbarButton(
    BrowserWindowInterface* browser)
    : ToolbarButton(base::BindRepeating(&AerieFilterToolbarButton::ButtonPressed,
                                        base::Unretained(this))),
      browser_(browser) {
  SetVectorIcon(vector_icons::kShieldIcon);
  GetViewAccessibility().SetName(u"Ad and tracker blocking");
  active_tab_subscription_ = browser_->RegisterActiveTabDidChange(
      base::BindRepeating(
          [](AerieFilterToolbarButton* self, BrowserWindowInterface*) {
            self->OnActiveTabChanged();
          },
          base::Unretained(this)));
  if (FilterListManager* manager = FilterListManager::Get()) {
    manager_observation_.Observe(manager);
  }
  OnActiveTabChanged();
}

AerieFilterToolbarButton::~AerieFilterToolbarButton() {
  if (observed_) {
    PageBlockCounter::RemoveObserverFor(observed_.get(), this);
  }
}

// static
std::u16string AerieFilterToolbarButton::LabelForCount(int count) {
  if (count <= 0) {
    return std::u16string();
  }
  return count > 99 ? u"99+" : base::NumberToString16(count);
}

content::WebContents* AerieFilterToolbarButton::ActiveWebContents() const {
  tabs::TabInterface* tab = browser_->GetActiveTabInterface();
  return tab ? tab->GetContents() : nullptr;
}

std::string AerieFilterToolbarButton::ActiveHost() const {
  content::WebContents* contents = ActiveWebContents();
  if (!contents) {
    return std::string();
  }
  const GURL& url = contents->GetLastCommittedURL();
  return url.SchemeIsHTTPOrHTTPS() ? std::string(url.host()) : std::string();
}

const void* AerieFilterToolbarButton::ActiveSession() const {
  content::WebContents* contents = ActiveWebContents();
  return contents ? SessionKeyFor(contents->GetBrowserContext()) : nullptr;
}

void AerieFilterToolbarButton::OnActiveTabChanged() {
  content::WebContents* contents = ActiveWebContents();
  if (contents != observed_.get()) {
    if (observed_) {
      PageBlockCounter::RemoveObserverFor(observed_.get(), this);
    }
    observed_ = contents ? contents->GetWeakPtr() : nullptr;
    if (observed_) {
      PageBlockCounter::AddObserverFor(observed_.get(), this);
    }
  }
  Update();
}

void AerieFilterToolbarButton::OnPageBlockedCountChanged(
    content::WebContents* web_contents,
    int count) {
  if (web_contents == observed_.get()) {
    Update();
  }
}

void AerieFilterToolbarButton::OnFilterListsChanged() {
  Update();
}

void AerieFilterToolbarButton::Update() {
  FilterListManager* manager = FilterListManager::Get();
  int count = PageBlockCounter::CountFor(observed_.get());
  std::string host = ActiveHost();
  bool off = manager && !host.empty() &&
             manager->HasException(host, ActiveSession());
  SetText(LabelForCount(count));
  std::u16string tooltip;
  if (off) {
    tooltip = u"Blocking is off on this site";
  } else if (count > 0) {
    tooltip = base::UTF8ToUTF16("Blocked on this page: " + std::to_string(count));
  } else {
    tooltip = u"Nothing blocked on this page";
  }
  SetTooltipText(tooltip);
  GetViewAccessibility().SetName(tooltip);
  SchedulePaint();
}

void AerieFilterToolbarButton::ButtonPressed() {
  FilterListManager* manager = FilterListManager::Get();
  std::string host = ActiveHost();
  menu_model_ = std::make_unique<ui::SimpleMenuModel>(this);
  if (manager && !host.empty()) {
    bool off = manager->HasException(host, ActiveSession());
    // An exception saved in a regular window cannot be undone from incognito.
    bool locked = off && ActiveSession() && manager->HasSavedException(host);
    menu_model_->AddItem(
        kToggleSite,
        base::UTF8ToUTF16(
            locked ? "Blocking is off here (turn it on in a regular window)"
                   : (off ? "Turn on blocking for " : "Turn off blocking for ") +
                         host +
                         (ActiveSession() ? " (this incognito session only)"
                                          : "")));
    menu_model_->SetEnabledAt(menu_model_->GetItemCount() - 1, !locked);
  }
  menu_model_->AddItem(kManageLists, u"Manage filter lists...");
  menu_runner_ = std::make_unique<views::MenuRunner>(
      menu_model_.get(), views::MenuRunner::HAS_MNEMONICS);
  menu_runner_->RunMenuAt(GetWidget(), nullptr, GetAnchorBoundsInScreen(),
                          views::MenuAnchorPosition::kTopRight,
                          ui::mojom::MenuSourceType::kMouse);
}

void AerieFilterToolbarButton::ExecuteCommand(int command_id, int event_flags) {
  FilterListManager* manager = FilterListManager::Get();
  switch (command_id) {
    case kToggleSite: {
      std::string host = ActiveHost();
      if (manager && !host.empty()) {
        const void* session = ActiveSession();
        if (session) {
          manager->WatchSession(ActiveWebContents()->GetBrowserContext());
        }
        manager->SetException(host, !manager->HasException(host, session),
                              session);
        // An excepted site loads its ads on the next load; the page that is open
        // keeps what it has. Reload so the change is visible at once.
        if (content::WebContents* contents = ActiveWebContents()) {
          contents->GetController().Reload(content::ReloadType::NORMAL, true);
        }
      }
      break;
    }
    case kManageLists:
      browser_->OpenGURL(GURL(kManagerPageUrl),
                         WindowOpenDisposition::NEW_FOREGROUND_TAB);
      break;
  }
}

BEGIN_METADATA(AerieFilterToolbarButton)
END_METADATA

}  // namespace aerie_adblock
