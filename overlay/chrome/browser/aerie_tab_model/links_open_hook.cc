// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/aerie_tab_model/links_open_hook.h"

#include <string>

#include "chrome/browser/aerie_tab_model/tab_model_service.h"
#include "chrome/browser/aerie_tab_model/tab_model_service_factory.h"
#include "chrome/browser/aerie_tab_model/tab_strip_adapter.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "components/aerie_tab_model/folder_behaviour.h"
#include "components/aerie_tab_model/tab_model.h"
#include "components/aerie_tab_model/tab_model_prefs.h"
#include "components/prefs/pref_service.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/web_contents.h"
#include "services/network/public/mojom/web_sandbox_flags.mojom.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"

namespace aerie_tab_model {

namespace {

// True when no frame of the page is sandboxed against navigating the top
// level. A sandboxed frame may open a new tab (allow-popups) but must not take
// the tab over, and the browser does not know which frame the click came from.
bool NoFrameIsSandboxed(content::WebContents* contents) {
  bool sandboxed = false;
  contents->GetPrimaryMainFrame()->ForEachRenderFrameHost(
      [&sandboxed](content::RenderFrameHost* frame) {
        if (frame->IsSandboxed(network::mojom::WebSandboxFlags::kTopNavigation)) {
          sandboxed = true;
        }
      });
  return !sandboxed;
}

bool OpenInPlace(NavigateParams* params, content::WebContents* source) {
  if (!params->browser || !params->url.SchemeIsHTTPOrHTTPS() ||
      !NoFrameIsSandboxed(source)) {
    return false;
  }
  // The frame that was clicked has focus; a link in an embedded frame is left
  // to open a new tab, so that a frame cannot take the tab over.
  content::RenderFrameHost* initiator = source->GetFocusedFrame();
  if (!initiator) {
    initiator = source->GetPrimaryMainFrame();
  }
  if (!initiator->IsInPrimaryMainFrame()) {
    return false;
  }
  NavigateParams in_place(params->browser, params->url,
                          ui::PAGE_TRANSITION_LINK);
  in_place.disposition = WindowOpenDisposition::CURRENT_TAB;
  in_place.source_contents = source;
  in_place.is_renderer_initiated = true;
  in_place.initiator_origin = initiator->GetLastCommittedOrigin();
  in_place.initiator_frame_token = initiator->GetFrameToken();
  in_place.initiator_process_id = initiator->GetProcess()->GetID();
  in_place.user_gesture = true;
  in_place.original_user_gesture = true;
  // The new contents that the page asked for is not needed.
  params->contents_to_insert.reset();
  chrome::Navigate(&in_place);
  return true;
}

}  // namespace

bool HandleLinkOpen(NavigateParams* params) {
  if (params->disposition != WindowOpenDisposition::NEW_FOREGROUND_TAB ||
      !params->contents_to_insert || !params->source_contents ||
      !params->original_user_gesture || params->contents_to_insert->HasOpener()) {
    return false;
  }
  content::WebContents* source = params->source_contents;
  tabs::TabInterface* tab = tabs::TabInterface::MaybeGetFromContents(source);
  Profile* profile = Profile::FromBrowserContext(source->GetBrowserContext());
  TabModelService* service = TabModelServiceFactory::GetForProfile(profile);
  TabStripAdapter* adapter = tab && service ? service->adapter() : nullptr;
  if (!adapter) {
    return false;
  }
  const std::string id = adapter->RecordIdForTab(tab);
  const TabRecord* record = id.empty() ? nullptr : service->model()->GetTab(id);
  if (!record) {
    return false;
  }
  const Resolved<LinksOpen> links = ResolveLinksOpen(
      *service->model(), *record, GetGlobalBehaviour(*profile->GetPrefs()));
  if (links.value == LinksOpen::kNewTab) {
    // Only a folder that says so moves the new tab; the global value changes
    // nothing about where a tab goes.
    if (!links.source_folder.empty()) {
      adapter->PlaceNextTabAfter(id);
    }
    return false;
  }
  return OpenInPlace(params, source);
}

}  // namespace aerie_tab_model
