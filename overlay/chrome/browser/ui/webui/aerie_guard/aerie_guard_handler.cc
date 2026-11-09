// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/ui/webui/aerie_guard/aerie_guard_handler.h"

#include <string>
#include <utility>

#include "base/notreached.h"
#include "base/strings/escape.h"
#include "base/strings/strcat.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/helium_noise/helium_noise_settings.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/chrome_pages.h"
#include "chrome/common/webui_url_constants.h"
#include "chrome/grit/generated_resources.h"
#include "components/aerie_fingerprint/protected_surfaces.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/url_constants.h"
#include "mojo/public/cpp/bindings/message.h"
#include "ui/base/l10n/l10n_util.h"
#include "url/gurl.h"

namespace aerie_guard {

namespace {

// The Aerie Guard section of Settings (issue #103), under chrome://settings/.
// Settings always opens in the regular profile, also from an incognito window.
constexpr char kAerieGuardSubPage[] = "aerieGuard";

// The favicon of |page| from the local favicon store. The Google server
// fallback is off: reading the icon makes no network request.
std::string FaviconUrl(const GURL& page) {
  return base::StrCat({content::kChromeUIScheme,
                       "://favicon2/?size=16&scaleFactor=2x&"
                       "allowGoogleServerFallback=0&pageUrl=",
                       base::EscapeQueryParamValue(page.GetWithEmptyPath().spec(),
                                                   /*use_plus=*/false)});
}

mojom::ProtectedSurface ToMojo(aerie_fingerprint::ProtectedSurface surface) {
  switch (surface) {
    case aerie_fingerprint::ProtectedSurface::kCanvas:
      return mojom::ProtectedSurface::kCanvas;
    case aerie_fingerprint::ProtectedSurface::kAudio:
      return mojom::ProtectedSurface::kAudio;
    case aerie_fingerprint::ProtectedSurface::kCpuCores:
      return mojom::ProtectedSurface::kCpuCores;
    case aerie_fingerprint::ProtectedSurface::kDeviceMemory:
      return mojom::ProtectedSurface::kDeviceMemory;
    case aerie_fingerprint::ProtectedSurface::kGraphicsCard:
      return mojom::ProtectedSurface::kGraphicsCard;
    case aerie_fingerprint::ProtectedSurface::kScreen:
      return mojom::ProtectedSurface::kScreen;
  }
  NOTREACHED();
}

mojom::FingerprintingMode ToMojo(helium_noise::HeliumNoiseState state) {
  switch (state) {
    case helium_noise::HeliumNoiseState::kEnabled:
      return mojom::FingerprintingMode::kOn;
    case helium_noise::HeliumNoiseState::kDisabledForSite:
      return mojom::FingerprintingMode::kOffForSite;
    case helium_noise::HeliumNoiseState::kGloballyDisabled:
      return mojom::FingerprintingMode::kOffInSettings;
    case helium_noise::HeliumNoiseState::kUnavailable:
      return mojom::FingerprintingMode::kUnavailable;
  }
  NOTREACHED();
}

// The committed URL of the tab's primary main frame: what the browser knows,
// never something the page sent.
GURL CommittedUrl(content::WebContents* tab) {
  return tab->GetPrimaryMainFrame()->GetLastCommittedURL();
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
    // Plain strings, read from the captured tab's counter: the page names
    // nothing here.
    out->blocked_urls =
        aerie_adblock::PageBlockCounter::BlockedUrlsFor(target_.tab());
  }
  out->fingerprinting = FingerprintingStateOfTarget();
  return out;
}

mojom::FingerprintingStatePtr AerieGuardHandler::FingerprintingStateOfTarget() {
  content::WebContents* tab = target_.tab();
  const helium_noise::HeliumNoiseState state =
      helium_noise::GetHeliumNoiseState(tab->GetBrowserContext(),
                                        CommittedUrl(tab));
  auto out = mojom::FingerprintingState::New();
  out->mode = ToMojo(state);
  for (aerie_fingerprint::ProtectedSurface surface :
       aerie_fingerprint::ProtectedSurfaces()) {
    out->surfaces.push_back(ToMojo(surface));
  }
  const bool on = state == helium_noise::HeliumNoiseState::kEnabled;
  if (state == helium_noise::HeliumNoiseState::kEnabled ||
      state == helium_noise::HeliumNoiseState::kDisabledForSite) {
    if (!loaded_fingerprinting_on_) {
      loaded_fingerprinting_on_ = on;
    }
    out->needs_reload = *loaded_fingerprinting_on_ != on;
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

void AerieGuardHandler::SetFingerprintingOn(bool on) {
  // A page with no target has nothing to change.
  if (target_.empty()) {
    return;
  }
  // Refuse, and close the panel, unless the tab is alive and still shows the
  // host the panel captured.
  if (!target_.Resolve()) {
    ClosePanel();
    return;
  }
  content::WebContents* tab = target_.tab();
  const GURL url = CommittedUrl(tab);
  // Only a state that has a switch can change: not "off in Settings" (the
  // switch is in Settings) and not "unavailable".
  const helium_noise::HeliumNoiseState state =
      helium_noise::GetHeliumNoiseState(tab->GetBrowserContext(), url);
  if (helium_noise::CanSetHeliumNoiseForOrigin(url) &&
      (state == helium_noise::HeliumNoiseState::kEnabled ||
       state == helium_noise::HeliumNoiseState::kDisabledForSite)) {
    // For an incognito tab this is the off-the-record content-settings map:
    // in memory, and the regular profile never sees it.
    helium_noise::SetHeliumNoiseEnabledForOrigin(tab->GetBrowserContext(), url,
                                                 on);
  }
  // The page shows the state as it is now: a refused switch snaps back.
  PushState();
}

void AerieGuardHandler::Reload() {
  if (target_.empty()) {
    return;
  }
  if (!target_.Resolve()) {
    ClosePanel();
    return;
  }
  target_.tab()->GetController().Reload(content::ReloadType::NORMAL, true);
  ClosePanel();
}

void AerieGuardHandler::OpenLink(mojom::Link link) {
  // Mojo already rejects a value that is not in the enum. This switch keeps
  // the browser's own answer if one is ever added and not handled here.
  switch (link) {
    case mojom::Link::kAerieGuardSettings:
    case mojom::Link::kNoiseSettings: {
      if (!target_.Resolve()) {
        if (!target_.empty()) {
          ClosePanel();
        }
        return;
      }
      tabs::TabInterface* tab =
          tabs::TabInterface::MaybeGetFromContents(target_.tab());
      if (tab && tab->GetBrowserWindowInterface()) {
        if (link == mojom::Link::kNoiseSettings) {
          chrome::ShowSettingsSubPage(tab->GetBrowserWindowInterface(),
                                      chrome::kNoiseSubPage);
        } else {
          chrome::ShowSettingsSubPage(tab->GetBrowserWindowInterface(),
                                      kAerieGuardSubPage);
        }
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
