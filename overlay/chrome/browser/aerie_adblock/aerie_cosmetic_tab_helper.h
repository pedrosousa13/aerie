// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_AERIE_ADBLOCK_AERIE_COSMETIC_TAB_HELPER_H_
#define CHROME_BROWSER_AERIE_ADBLOCK_AERIE_COSMETIC_TAB_HELPER_H_

#include <string>

#include "base/memory/weak_ptr.h"
#include "base/values.h"
#include "content/public/browser/global_routing_id.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"
#include "url/gurl.h"

// Applies cosmetic filtering (issue #9) to the pages of one tab: it hides the
// elements that the Filter Engine's hide rules name, so a blocked ad leaves no
// empty box.
//
// How, in the browser process only:
//   1. When a frame commits, ask the Filter Engine service for the
//      stylesheet for that URL and inject it into the frame.
//   2. When the frame's DOM is ready and again when it has loaded, read its
//      class names and ids, ask for the generic rules that match them and
//      inject those.
// Injection is a constructed stylesheet added to the document from an isolated
// world (see components/aerie_adblock/browser/cosmetic_scripts.h). Nothing a
// page controls is ever put in a script, and the only CSS is `display:none`
// rules for selectors that the Rust core checked.
//
// Whether cosmetic filtering is on for a site is the service's decision
// (FilterEngineService::SetCosmeticFilteringPolicy). A frame answers to the
// top-level page's setting. Not covered: elements added after the load event
// whose class or id was not on the page by then; a policy change reaches
// pages on their next navigation.
//
// Attached to each tab by patches/adblock/0007-attach-cosmetic-tab-helper.patch.
class AerieCosmeticTabHelper
    : public content::WebContentsObserver,
      public content::WebContentsUserData<AerieCosmeticTabHelper> {
 public:
  ~AerieCosmeticTabHelper() override;

  AerieCosmeticTabHelper(const AerieCosmeticTabHelper&) = delete;
  AerieCosmeticTabHelper& operator=(const AerieCosmeticTabHelper&) = delete;

  // content::WebContentsObserver:
  void DidFinishNavigation(content::NavigationHandle* handle) override;
  void DOMContentLoaded(content::RenderFrameHost* render_frame_host) override;
  void DidFinishLoad(content::RenderFrameHost* render_frame_host,
                     const GURL& validated_url) override;

 private:
  friend class content::WebContentsUserData<AerieCosmeticTabHelper>;
  explicit AerieCosmeticTabHelper(content::WebContents* web_contents);

  // True for frames of web pages that cosmetic filtering looks at.
  static bool ShouldFilter(content::RenderFrameHost* rfh);

  void InjectPageStylesheet(content::RenderFrameHost* rfh);
  void CollectAndInjectGenericStylesheet(content::RenderFrameHost* rfh);
  void OnTokensCollected(content::GlobalRenderFrameHostId id,
                         GURL page_url,
                         base::Value result);
  void Inject(content::GlobalRenderFrameHostId id,
              GURL page_url,
              std::string stylesheet);

  base::WeakPtrFactory<AerieCosmeticTabHelper> weak_factory_{this};

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

#endif  // CHROME_BROWSER_AERIE_ADBLOCK_AERIE_COSMETIC_TAB_HELPER_H_
