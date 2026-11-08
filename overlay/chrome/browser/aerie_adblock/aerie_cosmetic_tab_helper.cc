// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/aerie_adblock/aerie_cosmetic_tab_helper.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "components/aerie_adblock/browser/cosmetic_scripts.h"
#include "components/aerie_adblock/browser/filter_engine_service.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "url/origin.h"

using aerie_adblock::FilterEngineService;

namespace {

// The URL of the page that a frame is part of. Its setting decides for the
// frame, so an ad iframe is not judged by its own host.
GURL TopLevelUrl(content::RenderFrameHost* rfh) {
  return rfh->GetOutermostMainFrame()->GetLastCommittedURL();
}

// True if |rfh| still shows a page of the site that an answer was asked for.
// The site, not the exact URL: the page may change its URL with
// history.pushState while the engine works.
bool StillAt(content::RenderFrameHost* rfh, const GURL& page_url) {
  return url::IsSameOriginWith(rfh->GetLastCommittedURL(), page_url);
}

}  // namespace

AerieCosmeticTabHelper::AerieCosmeticTabHelper(
    content::WebContents* web_contents)
    : content::WebContentsObserver(web_contents),
      content::WebContentsUserData<AerieCosmeticTabHelper>(*web_contents) {}

AerieCosmeticTabHelper::~AerieCosmeticTabHelper() = default;

// static
bool AerieCosmeticTabHelper::ShouldFilter(content::RenderFrameHost* rfh) {
  return rfh && rfh->IsRenderFrameLive() &&
         rfh->GetLastCommittedURL().SchemeIsHTTPOrHTTPS() &&
         rfh->GetLifecycleState() !=
             content::RenderFrameHost::LifecycleState::kPrerendering;
}

void AerieCosmeticTabHelper::DidFinishNavigation(
    content::NavigationHandle* handle) {
  // Same-document navigations and pages restored from the back-forward cache
  // keep their document, and the stylesheet with it.
  if (!handle->HasCommitted() || handle->IsErrorPage() ||
      handle->IsSameDocument() || handle->IsServedFromBackForwardCache()) {
    return;
  }
  content::RenderFrameHost* rfh = handle->GetRenderFrameHost();
  if (!ShouldFilter(rfh)) {
    return;
  }
  InjectPageStylesheet(rfh);
  if (handle->IsPrerenderedPageActivation()) {
    // The document loaded while it was hidden; no load event will come now.
    CollectAndInjectGenericStylesheet(rfh);
  }
}

void AerieCosmeticTabHelper::DOMContentLoaded(
    content::RenderFrameHost* render_frame_host) {
  if (ShouldFilter(render_frame_host)) {
    CollectAndInjectGenericStylesheet(render_frame_host);
  }
}

void AerieCosmeticTabHelper::DidFinishLoad(
    content::RenderFrameHost* render_frame_host,
    const GURL& validated_url) {
  // Scripts add elements until the load event.
  if (ShouldFilter(render_frame_host)) {
    CollectAndInjectGenericStylesheet(render_frame_host);
  }
}

void AerieCosmeticTabHelper::InjectPageStylesheet(
    content::RenderFrameHost* rfh) {
  FilterEngineService* service = FilterEngineService::Get();
  if (!service) {
    return;
  }
  GURL page_url = rfh->GetLastCommittedURL();
  service->GetCosmeticStylesheet(
      TopLevelUrl(rfh), page_url,
      base::BindOnce(&AerieCosmeticTabHelper::Inject,
                     weak_factory_.GetWeakPtr(), rfh->GetGlobalId(),
                     page_url, aerie_adblock::SheetKind::kPage),
      aerie_adblock::SessionKeyFor(rfh->GetBrowserContext()));
}

void AerieCosmeticTabHelper::CollectAndInjectGenericStylesheet(
    content::RenderFrameHost* rfh) {
  if (!FilterEngineService::Get() ||
      !FilterEngineService::Get()->IsCosmeticFilteringEnabled(
          TopLevelUrl(rfh),
          aerie_adblock::SessionKeyFor(rfh->GetBrowserContext()))) {
    return;
  }
  rfh->ExecuteJavaScriptInIsolatedWorld(
      aerie_adblock::CollectClassesAndIdsScript(),
      base::BindOnce(&AerieCosmeticTabHelper::OnTokensCollected,
                     weak_factory_.GetWeakPtr(), rfh->GetGlobalId(),
                     rfh->GetLastCommittedURL()),
      aerie_adblock::kCosmeticWorldId);
}

void AerieCosmeticTabHelper::OnTokensCollected(
    content::GlobalRenderFrameHostId id,
    GURL page_url,
    base::Value result) {
  // The frame may have navigated or closed while the script ran.
  content::RenderFrameHost* rfh = content::RenderFrameHost::FromID(id);
  FilterEngineService* service = FilterEngineService::Get();
  if (!service || !ShouldFilter(rfh) || !StillAt(rfh, page_url)) {
    return;
  }
  auto tokens = aerie_adblock::ParsePageTokens(result);
  if (!tokens || (tokens->classes.empty() && tokens->ids.empty())) {
    return;
  }
  service->GetGenericStylesheet(
      TopLevelUrl(rfh), page_url, std::move(tokens->classes),
      std::move(tokens->ids),
      base::BindOnce(&AerieCosmeticTabHelper::Inject,
                     weak_factory_.GetWeakPtr(), id, page_url,
                     aerie_adblock::SheetKind::kGeneric),
      aerie_adblock::SessionKeyFor(rfh->GetBrowserContext()));
}

void AerieCosmeticTabHelper::Inject(content::GlobalRenderFrameHostId id,
                                    GURL page_url,
                                    aerie_adblock::SheetKind kind,
                                    std::string stylesheet) {
  if (stylesheet.empty()) {
    return;
  }
  content::RenderFrameHost* rfh = content::RenderFrameHost::FromID(id);
  if (!ShouldFilter(rfh) || !StillAt(rfh, page_url)) {
    return;  // Navigated away while the engine worked.
  }
  std::u16string script = aerie_adblock::BuildInjectionScript(stylesheet, kind);
  if (script.empty()) {
    return;
  }
  rfh->ExecuteJavaScriptInIsolatedWorld(
      script, base::DoNothing(), aerie_adblock::kCosmeticWorldId);
}

WEB_CONTENTS_USER_DATA_KEY_IMPL(AerieCosmeticTabHelper);
