// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/aerie_adblock/aerie_cosmetic_tab_helper.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/time/time.h"
#include "components/aerie_adblock/browser/cosmetic_scripts.h"
#include "components/aerie_adblock/browser/filter_engine_service.h"
#include "components/aerie_adblock/common/cosmetic_limits.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "mojo/public/cpp/bindings/pending_associated_receiver.h"
#include "third_party/blink/public/common/associated_interfaces/associated_interface_provider.h"
#include "third_party/blink/public/common/associated_interfaces/associated_interface_registry.h"

using aerie_adblock::FilterEngineService;
namespace mojom = aerie_adblock::mojom;

namespace {

// The URL of the page that a frame is part of. Its setting decides for the
// frame, so an ad iframe is not judged by its own host.
GURL TopLevelUrl(content::RenderFrameHost* rfh) {
  return rfh->GetOutermostMainFrame()->GetLastCommittedURL();
}

}  // namespace

AerieCosmeticTabHelper::AerieCosmeticTabHelper(
    content::WebContents* web_contents)
    : content::WebContentsObserver(web_contents),
      content::WebContentsUserData<AerieCosmeticTabHelper>(*web_contents),
      late_receivers_(web_contents, this) {}

AerieCosmeticTabHelper::~AerieCosmeticTabHelper() = default;

// static
void AerieCosmeticTabHelper::RegisterBinder(
    blink::AssociatedInterfaceRegistry& registry,
    content::RenderFrameHost& render_frame_host) {
  registry.AddInterface<mojom::CosmeticHost>(base::BindRepeating(
      &AerieCosmeticTabHelper::BindHost, &render_frame_host));
}

// static
void AerieCosmeticTabHelper::BindHost(
    content::RenderFrameHost* render_frame_host,
    mojo::PendingAssociatedReceiver<mojom::CosmeticHost> receiver) {
  auto* web_contents =
      content::WebContents::FromRenderFrameHost(render_frame_host);
  auto* helper =
      web_contents ? AerieCosmeticTabHelper::FromWebContents(web_contents)
                   : nullptr;
  if (helper) {
    helper->late_receivers_.Bind(render_frame_host, std::move(receiver));
  }
}

// static
bool AerieCosmeticTabHelper::ShouldFilter(content::RenderFrameHost* rfh) {
  return rfh && rfh->IsRenderFrameLive() && rfh->IsActive() &&
         rfh->GetLastCommittedURL().SchemeIsHTTPOrHTTPS();
}

// static
FilterEngineService* AerieCosmeticTabHelper::ServiceIfEnabled(
    content::RenderFrameHost* rfh) {
  FilterEngineService* service = FilterEngineService::Get();
  if (!service ||
      !service->IsCosmeticFilteringEnabled(
          TopLevelUrl(rfh),
          aerie_adblock::SessionKeyFor(rfh->GetBrowserContext()))) {
    return nullptr;
  }
  return service;
}

void AerieCosmeticTabHelper::DidFinishNavigation(
    content::NavigationHandle* handle) {
  // Same-document navigations and pages restored from the back-forward cache
  // keep their document, and the stylesheet with it.
  //
  // Known edge: a page that sits in the back-forward cache keeps its page sheet
  // (and generic sheet) as they were. The sheets were made for that document, so
  // restoring it is right, but a policy or exception that changed while it was
  // cached is not applied until its next real navigation (the same rule as for
  // any open page, ADR 0005). Nothing is sent to a cached frame: it is not
  // active, so ShouldFilter() refuses it.
  if (!handle->HasCommitted() || handle->IsErrorPage() ||
      handle->IsSameDocument() || handle->IsServedFromBackForwardCache()) {
    return;
  }
  content::RenderFrameHost* rfh = handle->GetRenderFrameHost();
  if (!ShouldFilter(rfh)) {
    return;
  }
  // A new document: its late reports start counting again.
  gates_.erase(rfh->GetGlobalId());
  InjectPageStylesheet(rfh);
  if (handle->IsPrerenderedPageActivation()) {
    // The document loaded while it was hidden; no load event will come now.
    InstallLateWatch(rfh);
    CollectAndInjectGenericStylesheet(rfh);
  }
}

void AerieCosmeticTabHelper::DOMContentLoaded(
    content::RenderFrameHost* render_frame_host) {
  if (ShouldFilter(render_frame_host)) {
    // The watch goes first: whatever is inserted after it is reported, and
    // whatever is there when the collect script runs is read.
    InstallLateWatch(render_frame_host);
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

void AerieCosmeticTabHelper::RenderFrameDeleted(
    content::RenderFrameHost* render_frame_host) {
  agents_.erase(render_frame_host->GetGlobalId());
  gates_.erase(render_frame_host->GetGlobalId());
}

mojom::CosmeticAgent* AerieCosmeticTabHelper::AgentFor(
    content::RenderFrameHost* rfh) {
  auto& agent = agents_[rfh->GetGlobalId()];
  if (!agent.is_bound()) {
    rfh->GetRemoteAssociatedInterfaces()->GetInterface(&agent);
  }
  return agent.get();
}

void AerieCosmeticTabHelper::InjectPageStylesheet(
    content::RenderFrameHost* rfh) {
  FilterEngineService* service = FilterEngineService::Get();
  if (!service) {
    return;
  }
  service->GetCosmeticStylesheet(
      TopLevelUrl(rfh), rfh->GetLastCommittedURL(),
      base::BindOnce(&AerieCosmeticTabHelper::SendRules,
                     weak_factory_.GetWeakPtr(), rfh->GetWeakDocumentPtr(),
                     mojom::CosmeticSheetKind::kPage, /*append=*/false),
      aerie_adblock::SessionKeyFor(rfh->GetBrowserContext()));
}

void AerieCosmeticTabHelper::InstallLateWatch(content::RenderFrameHost* rfh) {
  if (!ServiceIfEnabled(rfh)) {
    return;  // Blocking is off here: no watch, no reports.
  }
  rfh->ExecuteJavaScriptInIsolatedWorld(
      aerie_adblock::LateWatchScript(), base::DoNothing(),
      aerie_adblock::kCosmeticWorldId);
}

void AerieCosmeticTabHelper::CollectAndInjectGenericStylesheet(
    content::RenderFrameHost* rfh) {
  if (!ServiceIfEnabled(rfh)) {
    return;
  }
  rfh->ExecuteJavaScriptInIsolatedWorld(
      aerie_adblock::CollectClassesAndIdsScript(),
      base::BindOnce(&AerieCosmeticTabHelper::OnTokensCollected,
                     weak_factory_.GetWeakPtr(), rfh->GetWeakDocumentPtr()),
      aerie_adblock::kCosmeticWorldId);
}

void AerieCosmeticTabHelper::OnTokensCollected(
    content::WeakDocumentPtr document,
    base::Value result) {
  // The frame may have navigated or closed while the script ran.
  content::RenderFrameHost* rfh = document.AsRenderFrameHostIfValid();
  if (!ShouldFilter(rfh)) {
    return;
  }
  auto tokens = aerie_adblock::ParsePageTokens(result);
  if (!tokens || (tokens->classes.empty() && tokens->ids.empty())) {
    return;
  }
  AppendGenericRules(rfh, std::move(tokens->classes), std::move(tokens->ids));
}

void AerieCosmeticTabHelper::AppendGenericRules(content::RenderFrameHost* rfh,
                                                std::string classes,
                                                std::string ids) {
  FilterEngineService* service = FilterEngineService::Get();
  if (!service) {
    return;
  }
  // Every pass appends: the renderer keeps one generic sheet and does not
  // add a line twice, so the passes can come in any order.
  service->GetGenericStylesheet(
      TopLevelUrl(rfh), rfh->GetLastCommittedURL(), std::move(classes),
      std::move(ids),
      base::BindOnce(&AerieCosmeticTabHelper::SendRules,
                     weak_factory_.GetWeakPtr(), rfh->GetWeakDocumentPtr(),
                     mojom::CosmeticSheetKind::kGeneric, /*append=*/true),
      aerie_adblock::SessionKeyFor(rfh->GetBrowserContext()));
}

void AerieCosmeticTabHelper::LateTokens(const std::string& classes,
                                        const std::string& ids) {
  content::RenderFrameHost* rfh = &late_receivers_.CurrentTargetFrame();
  // The renderer already drops a report this big, so one that arrives is not
  // from the agent. The size is checked here, and not only in the gate, because
  // an oversized report is a bad message that ends the renderer, while a report
  // that is merely too frequent is ignored.
  if (classes.size() > aerie_adblock::kMaxLateTokenListBytes ||
      ids.size() > aerie_adblock::kMaxLateTokenListBytes) {
    late_receivers_.ReportBadMessage("Aerie cosmetic: oversized late tokens");
    return;
  }
  // Off here (or since the page loaded): ignore the report.
  if (!ShouldFilter(rfh) || !ServiceIfEnabled(rfh)) {
    return;
  }
  // The renderer sends one report per kLateMinIntervalMs at most. The gates
  // here are looser, because the messages do not arrive evenly: one for the
  // frame, and a token bucket for the whole tab. The bucket is only looked at
  // first and taken from last, so a report that the frame gate refuses costs
  // the tab nothing, and one that the tab refuses costs the frame nothing.
  const base::TimeTicks now = base::TimeTicks::Now();
  if (!tab_bucket_.HasToken(now)) {
    return;
  }
  auto frame_gate = gates_.try_emplace(
      rfh->GetGlobalId(),
      base::Milliseconds(aerie_adblock::kLateFrameGateIntervalMs));
  if (!frame_gate.first->second.Allow(classes, ids, now)) {
    return;
  }
  tab_bucket_.Take(now);
  AppendGenericRules(rfh, classes, ids);
}

void AerieCosmeticTabHelper::SendRules(
    content::WeakDocumentPtr document,
    mojom::CosmeticSheetKind kind,
    bool append,
    std::string stylesheet) {
  // Not the origin: a new document of the same site must not get the rules
  // that were asked for the old one.
  content::RenderFrameHost* rfh = document.AsRenderFrameHostIfValid();
  if (!ShouldFilter(rfh)) {
    return;
  }
  // No rules for a generic pass is nothing to do. No rules for the page is a
  // call too: it clears a page sheet that an earlier page left.
  if (stylesheet.empty() && kind != mojom::CosmeticSheetKind::kPage) {
    return;
  }
  if (mojom::CosmeticAgent* agent = AgentFor(rfh)) {
    agent->SetRules(kind, std::move(stylesheet), append);
  }
}

WEB_CONTENTS_USER_DATA_KEY_IMPL(AerieCosmeticTabHelper);
