// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_AERIE_GUARD_AERIE_GUARD_UI_H_
#define CHROME_BROWSER_UI_WEBUI_AERIE_GUARD_AERIE_GUARD_UI_H_

#include <memory>
#include <string_view>

#include "chrome/browser/ui/webui/aerie_guard/aerie_guard.mojom.h"
#include "chrome/browser/ui/webui/top_chrome/top_chrome_web_ui_controller.h"
#include "chrome/browser/ui/webui/top_chrome/top_chrome_webui_config.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"

namespace aerie_guard {

class AerieGuardHandler;
class AerieGuardUI;

// chrome://aerie-guard.top-chrome (issue #100): the panel that opens from the
// Guard icon in the address bar. Registered in chrome_web_ui_configs.cc by
// patches/adblock/0018.
class AerieGuardUIConfig : public DefaultTopChromeWebUIConfig<AerieGuardUI> {
 public:
  AerieGuardUIConfig();

  // DefaultTopChromeWebUIConfig:
  bool ShouldAutoResizeHost() override;
  // Never. A preloaded page would be created before a tab exists, or in a
  // profile that has no Guard icon, and the panel needs the tab and site its
  // icon captured.
  bool IsPreloadable() override;
};

// The controller. Mojo only: it has no chrome.send bindings and no
// WebUIMessageHandler, and binds one interface, PageHandlerFactory.
class AerieGuardUI : public TopChromeWebUIController,
                     public mojom::PageHandlerFactory {
 public:
  explicit AerieGuardUI(content::WebUI* web_ui);
  AerieGuardUI(const AerieGuardUI&) = delete;
  AerieGuardUI& operator=(const AerieGuardUI&) = delete;
  ~AerieGuardUI() override;

  static constexpr std::string_view GetWebUIName() { return "AerieGuard"; }

  // Binds the factory (see chrome_browser_interface_binders_webui_parts_desktop.cc).
  void BindInterface(
      mojo::PendingReceiver<mojom::PageHandlerFactory> receiver);

 private:
  // mojom::PageHandlerFactory:
  void CreatePageHandler(
      mojo::PendingRemote<mojom::Page> page,
      mojo::PendingReceiver<mojom::PageHandler> handler) override;

  std::unique_ptr<AerieGuardHandler> handler_;
  mojo::Receiver<mojom::PageHandlerFactory> factory_receiver_{this};

  WEB_UI_CONTROLLER_TYPE_DECL();
};

}  // namespace aerie_guard

#endif  // CHROME_BROWSER_UI_WEBUI_AERIE_GUARD_AERIE_GUARD_UI_H_
