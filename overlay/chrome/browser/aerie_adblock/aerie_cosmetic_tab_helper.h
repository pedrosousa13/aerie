// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_AERIE_ADBLOCK_AERIE_COSMETIC_TAB_HELPER_H_
#define CHROME_BROWSER_AERIE_ADBLOCK_AERIE_COSMETIC_TAB_HELPER_H_

#include <map>
#include <string>

#include "base/memory/weak_ptr.h"
#include "base/values.h"
#include "components/aerie_adblock/browser/cosmetic_scripts.h"
#include "components/aerie_adblock/browser/filter_engine_service.h"
#include "components/aerie_adblock/common/cosmetic.mojom.h"
#include "components/aerie_adblock/common/late_report_gate.h"
#include "content/public/browser/global_routing_id.h"
#include "content/public/browser/render_frame_host_receiver_set.h"
#include "content/public/browser/weak_document_ptr.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"
#include "mojo/public/cpp/bindings/associated_remote.h"
#include "url/gurl.h"

namespace blink {
class AssociatedInterfaceRegistry;
}

// Applies cosmetic filtering (issue #9) to the pages of one tab: it hides the
// elements that the Filter Engine's hide rules name, so a blocked ad leaves no
// empty box.
//
// The browser process decides everything:
//   1. When a frame commits, ask the Filter Engine service for the
//      stylesheet for that URL and send it to the frame's renderer.
//   2. When the frame's DOM is ready and again when it has loaded, read its
//      class names and ids, ask for the generic rules that match them and
//      send those.
//   3. From DOM ready on, a fixed script in the Aerie isolated world reports
//      class names and ids of elements that are inserted later (issue #68).
//      The report comes back through LateTokens(); the helper asks the engine
//      for the rules for those tokens and sends them.
//
// "Sending" is one call on the narrow Mojo interface in
// components/aerie_adblock/common/cosmetic.mojom. The renderer's
// AerieCosmeticAgent inserts the text as a user-origin sheet, which the page
// cannot see or remove (ADR 0005). It applies only what the browser sends.
// Nothing a page controls is ever put in a script, and the only CSS is
// `display:none` rules for selectors that the Rust core checked.
//
// Whether cosmetic filtering is on for a site is the service's decision
// (FilterEngineService::SetCosmeticFilteringPolicy), asked again for every
// late report. A frame answers to the top-level page's setting. A policy
// change reaches pages on their next navigation, except that it stops the late
// reports at once.
//
// Attached to each tab by patches/adblock/0007-attach-cosmetic-tab-helper.patch.
// The renderer's calls reach it through patches/adblock/0013.
class AerieCosmeticTabHelper
    : public content::WebContentsObserver,
      public content::WebContentsUserData<AerieCosmeticTabHelper>,
      public aerie_adblock::mojom::CosmeticHost {
 public:
  // Lets a frame's renderer reach the helper of its tab. Called for each
  // RenderFrameHost by ChromeContentBrowserClient (patch 0013).
  static void RegisterBinder(blink::AssociatedInterfaceRegistry& registry,
                             content::RenderFrameHost& render_frame_host);

  ~AerieCosmeticTabHelper() override;

  AerieCosmeticTabHelper(const AerieCosmeticTabHelper&) = delete;
  AerieCosmeticTabHelper& operator=(const AerieCosmeticTabHelper&) = delete;

  // content::WebContentsObserver:
  void DidFinishNavigation(content::NavigationHandle* handle) override;
  void DOMContentLoaded(content::RenderFrameHost* render_frame_host) override;
  void DidFinishLoad(content::RenderFrameHost* render_frame_host,
                     const GURL& validated_url) override;
  void RenderFrameDeleted(content::RenderFrameHost* render_frame_host) override;

  // aerie_adblock::mojom::CosmeticHost. The renderer is not trusted: the
  // arguments are bounded, rate limited and only used as lookup keys.
  void LateTokens(const std::string& classes, const std::string& ids) override;

 private:
  friend class content::WebContentsUserData<AerieCosmeticTabHelper>;
  explicit AerieCosmeticTabHelper(content::WebContents* web_contents);

  // True for frames of web pages that cosmetic filtering looks at: live,
  // http(s), not prerendering, and active (not in the back-forward cache and
  // not being deleted).
  static bool ShouldFilter(content::RenderFrameHost* rfh);
  // The service, if it exists and cosmetic filtering is on for the page that
  // |rfh| is part of (the policy and the per-site exception); null otherwise.
  static aerie_adblock::FilterEngineService* ServiceIfEnabled(
      content::RenderFrameHost* rfh);

  static void BindHost(
      content::RenderFrameHost* render_frame_host,
      mojo::PendingAssociatedReceiver<aerie_adblock::mojom::CosmeticHost>
          receiver);

  void InjectPageStylesheet(content::RenderFrameHost* rfh);
  // Starts the watch for late elements, if cosmetic filtering is on.
  void InstallLateWatch(content::RenderFrameHost* rfh);
  void CollectAndInjectGenericStylesheet(content::RenderFrameHost* rfh);
  void OnTokensCollected(content::WeakDocumentPtr document,
                         base::Value result);
  // Asks the engine for the generic rules that match |classes| and |ids| of
  // the page in |rfh|, and appends them to the frame's generic sheet.
  void AppendGenericRules(content::RenderFrameHost* rfh,
                          std::string classes,
                          std::string ids);
  // Hands |stylesheet| to the renderer of the frame that |document| was, if
  // that document is still the one shown (a later document of the same origin
  // is not). |append| adds to the frame's sheet of |kind|; otherwise the text
  // replaces it, and an empty text for the page sheet clears it.
  void SendRules(content::WeakDocumentPtr document,
                 aerie_adblock::mojom::CosmeticSheetKind kind,
                 bool append,
                 std::string stylesheet);
  aerie_adblock::mojom::CosmeticAgent* AgentFor(content::RenderFrameHost* rfh);

  content::RenderFrameHostReceiverSet<aerie_adblock::mojom::CosmeticHost>
      late_receivers_;
  // One Mojo pipe to each frame's agent, and one gate on each frame's late
  // reports (reset with each new document).
  std::map<content::GlobalRenderFrameHostId,
           mojo::AssociatedRemote<aerie_adblock::mojom::CosmeticAgent>>
      agents_;
  std::map<content::GlobalRenderFrameHostId, aerie_adblock::LateReportGate>
      gates_;
  // A token bucket for all the frames of the tab, on top of the per-frame
  // gates: a burst far above honest traffic, then a steady rate, and no lifetime
  // cap, so one frame cannot use up what the others need. A per-frame-origin cap
  // would add a map and a policy for nothing: the per-frame gate bounds each
  // frame, and the bucket the total rate.
  aerie_adblock::LateTokenBucket tab_bucket_{
      aerie_adblock::kLateTabBurst,
      base::Milliseconds(aerie_adblock::kLateTabRefillIntervalMs)};

  base::WeakPtrFactory<AerieCosmeticTabHelper> weak_factory_{this};

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

#endif  // CHROME_BROWSER_AERIE_ADBLOCK_AERIE_COSMETIC_TAB_HELPER_H_
