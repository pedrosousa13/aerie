// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_AERIE_GUARD_AERIE_GUARD_HANDLER_H_
#define CHROME_BROWSER_UI_WEBUI_AERIE_GUARD_AERIE_GUARD_HANDLER_H_

#include <optional>

#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "chrome/browser/ui/webui/aerie_guard/aerie_guard.mojom.h"
#include "chrome/browser/ui/webui/top_chrome/top_chrome_web_ui_controller.h"
#include "components/aerie_adblock/browser/filter_list_manager.h"
#include "components/aerie_adblock/browser/guard_target.h"
#include "components/aerie_adblock/browser/page_block_counter.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"

namespace aerie_guard {

// The browser side of the Guard panel (issue #100).
//
// The handler acts only on the |target| it is given, which the Guard icon
// captured in the browser process when the panel opened. Nothing the page sends
// names a tab, a host or a URL: the inputs are a bool and an enum. On every
// call the handler asks the target to Resolve(): if the tab is gone or shows
// another host, the call is refused, nothing changes and the panel closes.
//
// An empty target (the page loaded in an ordinary tab) reads nothing and
// changes nothing.
class AerieGuardHandler : public mojom::PageHandler,
                          public aerie_adblock::PageBlockCounter::Observer,
                          public aerie_adblock::FilterListManager::Observer {
 public:
  AerieGuardHandler(
      mojo::PendingReceiver<mojom::PageHandler> receiver,
      mojo::PendingRemote<mojom::Page> page,
      aerie_adblock::GuardTarget target,
      base::WeakPtr<TopChromeWebUIController::Embedder> embedder);
  AerieGuardHandler(const AerieGuardHandler&) = delete;
  AerieGuardHandler& operator=(const AerieGuardHandler&) = delete;
  ~AerieGuardHandler() override;

  // mojom::PageHandler:
  void GetState(GetStateCallback callback) override;
  void ShowUI() override;
  void SetGuardOn(bool on) override;
  void SetBlockingLevel(mojom::BlockingLevel level) override;
  void SetCosmeticHidingOn(bool on) override;
  void SetFingerprintingOn(bool on) override;
  void Reload() override;
  void OpenLink(mojom::Link link) override;

  // aerie_adblock::PageBlockCounter::Observer:
  void OnPageBlockedCountChanged(content::WebContents* web_contents,
                                 int count) override;
  // aerie_adblock::FilterListManager::Observer:
  void OnFilterListsChanged() override;

 private:
  // The state of the captured site, or null. Closes the panel when the target
  // was good once and is stale now.
  mojom::PanelStatePtr ResolveState();
  // The fingerprinting row of the captured tab's site now.
  mojom::FingerprintingStatePtr FingerprintingStateOfTarget();
  // Sends the current state to the page.
  void PushState();
  void ClosePanel();
  // Pushes the state after a target's change, or closes the panel when the
  // target went stale. Shared by the two switches.
  void FinishToggle(aerie_adblock::GuardTarget::Toggle result);

  const aerie_adblock::GuardTarget target_;
  // Whether fingerprinting protection was on for the site when the panel first
  // read it. The tab shows that state until it reloads, so the "Reload to
  // apply" prompt is shown while the switch differs from it.
  std::optional<bool> loaded_fingerprinting_on_;
  base::WeakPtr<TopChromeWebUIController::Embedder> embedder_;
  mojo::Receiver<mojom::PageHandler> receiver_;
  mojo::Remote<mojom::Page> page_;
  // The tab whose count this handler observes, if any.
  base::WeakPtr<content::WebContents> counted_tab_;
  base::ScopedObservation<aerie_adblock::FilterListManager,
                          aerie_adblock::FilterListManager::Observer>
      manager_observation_{this};
};

}  // namespace aerie_guard

#endif  // CHROME_BROWSER_UI_WEBUI_AERIE_GUARD_AERIE_GUARD_HANDLER_H_
