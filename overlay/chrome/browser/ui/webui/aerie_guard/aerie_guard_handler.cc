// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/ui/webui/aerie_guard/aerie_guard_handler.h"

#include <string>
#include <utility>

#include "base/strings/escape.h"
#include "base/strings/strcat.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/grit/generated_resources.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/url_constants.h"
#include "mojo/public/cpp/bindings/message.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/window_open_disposition.h"
#include "url/gurl.h"

namespace aerie_guard {

namespace {

// The page Filter lists... opens. It moves to the settings section of issue
// #103 when that exists.
constexpr char kFilterListsUrl[] = "chrome://filters/";

// The favicon of |page| from the local favicon store. The Google server
// fallback is off: reading the icon makes no network request.
std::string FaviconUrl(const GURL& page) {
  return base::StrCat({content::kChromeUIScheme,
                       "://favicon2/?size=16&scaleFactor=2x&"
                       "allowGoogleServerFallback=0&pageUrl=",
                       base::EscapeQueryParamValue(page.GetWithEmptyPath().spec(),
                                                   /*use_plus=*/false)});
}

}  // namespace

AerieGuardHandler::AerieGuardHandler(
    mojo::PendingReceiver<mojom::PageHandler> receiver,
    mojo::PendingRemote<mojom::Page> page,
    aerie_adblock::GuardTarget target,
    base::WeakPtr<TopChromeWebUIController::Embedder> embedder)
    : target_(std::move(target)),
      embedder_(std::move(embedder)),
      receiver_(this, std::move(receiver)),
      page_(std::move(page)) {
  if (content::WebContents* tab = target_.tab()) {
    counted_tab_ = tab->GetWeakPtr();
    aerie_adblock::PageBlockCounter::AddObserverFor(tab, this);
  }
  if (aerie_adblock::FilterListManager* manager =
          aerie_adblock::FilterListManager::Get()) {
    manager_observation_.Observe(manager);
  }
}

AerieGuardHandler::~AerieGuardHandler() {
  if (counted_tab_) {
    aerie_adblock::PageBlockCounter::RemoveObserverFor(counted_tab_.get(),
                                                       this);
  }
}

mojom::PanelStatePtr AerieGuardHandler::ResolveState() {
  if (target_.empty()) {
    return nullptr;
  }
  const std::optional<aerie_adblock::GuardState> state = target_.Resolve();
  if (!state) {
    // The tab closed or went to another site: the panel no longer describes
    // anything it may act on.
    ClosePanel();
    return nullptr;
  }
  auto out = mojom::PanelState::New();
  out->host = state->host;
  out->favicon_url = FaviconUrl(
      target_.tab()->GetPrimaryMainFrame()->GetLastCommittedURL());
  out->blocking_on = !state->blocking_off;
  out->incognito = target_.tab()->GetBrowserContext()->IsOffTheRecord();
  out->locked = state->locked;
  out->cosmetic_hiding_on = !state->cosmetic_off;
  out->cosmetic_locked = state->cosmetic_locked;
  out->blocked_count = state->count;
  if (out->blocking_on) {
    out->blocked_summary = base::UTF16ToUTF8(l10n_util::GetPluralStringFUTF16(
        IDS_AERIE_GUARD_PANEL_BLOCKED, state->count));
  }
  return out;
}

void AerieGuardHandler::PushState() {
  page_->OnStateChanged(ResolveState());
}

void AerieGuardHandler::ClosePanel() {
  if (embedder_) {
    embedder_->CloseUI();
  }
}

void AerieGuardHandler::GetState(GetStateCallback callback) {
  std::move(callback).Run(ResolveState());
}

void AerieGuardHandler::ShowUI() {
  // A page with no target is not in a bubble: there is nothing to show.
  if (target_.empty() || !embedder_) {
    return;
  }
  if (!target_.Resolve()) {
    ClosePanel();
    return;
  }
  embedder_->ShowUI();
}

void AerieGuardHandler::FinishToggle(
    aerie_adblock::GuardTarget::Toggle result) {
  switch (result) {
    case aerie_adblock::GuardTarget::Toggle::kApplied:
    case aerie_adblock::GuardTarget::Toggle::kRefused:
      // The page shows the state as it is now: a refused switch snaps back.
      PushState();
      break;
    case aerie_adblock::GuardTarget::Toggle::kStale:
      // Nothing changed. A panel with a target that went stale closes; a page
      // with no target has no bubble to close.
      if (!target_.empty()) {
        ClosePanel();
      }
      break;
  }
}

void AerieGuardHandler::SetGuardOn(bool on) {
  FinishToggle(target_.SetBlockingOn(on));
}

void AerieGuardHandler::SetCosmeticHidingOn(bool on) {
  FinishToggle(target_.SetCosmeticHidingOn(on));
}

void AerieGuardHandler::OpenLink(mojom::Link link) {
  // Mojo already rejects a value that is not in the enum. This switch keeps
  // the browser's own answer if one is ever added and not handled here.
  switch (link) {
    case mojom::Link::kFilterLists: {
      if (!target_.Resolve()) {
        if (!target_.empty()) {
          ClosePanel();
        }
        return;
      }
      tabs::TabInterface* tab =
          tabs::TabInterface::MaybeGetFromContents(target_.tab());
      if (tab && tab->GetBrowserWindowInterface()) {
        tab->GetBrowserWindowInterface()->OpenGURL(
            GURL(kFilterListsUrl), WindowOpenDisposition::NEW_FOREGROUND_TAB);
      }
      ClosePanel();
      return;
    }
  }
  mojo::ReportBadMessage("Aerie Guard: unknown link");
}

void AerieGuardHandler::OnPageBlockedCountChanged(
    content::WebContents* web_contents,
    int count) {
  if (web_contents == counted_tab_.get()) {
    PushState();
  }
}

void AerieGuardHandler::OnFilterListsChanged() {
  PushState();
}

}  // namespace aerie_guard
