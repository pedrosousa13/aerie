// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

// The isolation of the Guard panel (issue #100): no renderer of an ordinary
// page can bind its Mojo interface, no web page can navigate to it, and the
// page has no chrome.send. And the life of the bubble: the icon opens it again
// after it closed, and a tab can close while it is open.

#include <memory>
#include <optional>
#include <string>

#include "base/functional/bind.h"
#include "base/run_loop.h"
#include "base/values.h"
#include "base/test/bind.h"
#include "chrome/browser/chrome_browser_interface_binders.h"
#include "chrome/browser/content_settings/host_content_settings_map_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/pref_names.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/webui/aerie_guard/aerie_guard_handler.h"
#include "components/aerie_adblock/browser/guard_target.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"
#include "components/content_settings/core/common/content_settings.h"
#include "components/content_settings/core/common/content_settings_types.h"
#include "components/prefs/pref_service.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "chrome/browser/ui/tabs/public/tab_features.h"
#include "chrome/browser/ui/views/aerie_adblock/aerie_guard_page_action_controller.h"
#include "chrome/browser/ui/webui/aerie_guard/aerie_guard.mojom.h"
#include "chrome/common/webui_url_constants.h"
#include "chrome/test/base/chrome_test_utils.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/test_navigation_observer.h"
#include "content/public/test/test_utils.h"
#include "mojo/public/cpp/bindings/binder_map.h"
#include "mojo/public/cpp/bindings/generic_pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "ui/base/window_open_disposition.h"
#include "ui/views/bubble/bubble_anchor.h"
#include "ui/views/test/widget_test.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace {

class AerieGuardBrowserTest : public InProcessBrowserTest {
 public:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    host_resolver()->AddRule("*", "127.0.0.1");
    ASSERT_TRUE(embedded_test_server()->Start());
  }

  content::WebContents* web_contents() {
    return chrome_test_utils::GetActiveWebContents(this);
  }

  // The Guard icon's controller of the active tab.
  AerieGuardPageActionController* guard() {
    return tabs::TabInterface::GetFromContents(web_contents())
        ->GetTabFeatures()
        ->aerie_guard_page_action_controller();
  }

  // Something in the browser window to anchor the bubble to, as the icon is.
  views::BubbleAnchor anchor() {
    return views::BubbleAnchor(
        views::Widget::GetWidgetForNativeWindow(
            web_contents()->GetTopLevelNativeWindow())
            ->GetRootView());
  }

  // Presses the icon while the panel is open, as a user does to close it, and
  // waits until its widget is gone.
  void PressToClose() {
    views::Widget* panel = guard()->panel_widget_for_testing();
    ASSERT_TRUE(panel);
    views::test::WidgetDestroyedWaiter closed(panel);
    guard()->ShowPanel(anchor());
    closed.Wait();
    EXPECT_FALSE(guard()->panel_widget_for_testing());
  }
};

// The binders every frame gets (the ones a normal renderer can reach) do not
// include the panel's interfaces. They are bound only for the panel's own WebUI
// (chrome_browser_interface_binders_webui_parts_desktop.cc).
IN_PROC_BROWSER_TEST_F(AerieGuardBrowserTest,
                       AnOrdinaryFrameCannotBindThePanelsInterfaces) {
  ASSERT_TRUE(content::NavigateToURL(
      web_contents(), embedded_test_server()->GetURL("news.test", "/title1.html")));
  content::RenderFrameHost* frame = web_contents()->GetPrimaryMainFrame();

  mojo::BinderMapWithContext<content::RenderFrameHost*> binders;
  chrome::internal::PopulateChromeFrameBinders(&binders, frame);

  mojo::Remote<aerie_guard::mojom::PageHandlerFactory> factory;
  mojo::GenericPendingReceiver factory_receiver(
      factory.BindNewPipeAndPassReceiver());
  EXPECT_FALSE(binders.TryBind(frame, &factory_receiver));

  // The handler and the page are never bound by name, either.
  mojo::Remote<aerie_guard::mojom::PageHandler> handler;
  mojo::GenericPendingReceiver handler_receiver(
      handler.BindNewPipeAndPassReceiver());
  EXPECT_FALSE(binders.TryBind(frame, &handler_receiver));
}

// A web page cannot navigate to chrome://aerie-guard.top-chrome/: not by a
// script, and not by a link.
IN_PROC_BROWSER_TEST_F(AerieGuardBrowserTest,
                       AWebPageCannotNavigateToThePanel) {
  const GURL page = embedded_test_server()->GetURL("news.test", "/title1.html");
  ASSERT_TRUE(content::NavigateToURL(web_contents(), page));

  EXPECT_TRUE(content::ExecJs(web_contents(),
                              "location.href = '" +
                                  std::string(chrome::kChromeUIAerieGuardURL) +
                                  "';"));
  content::RunAllTasksUntilIdle();
  EXPECT_EQ(page, web_contents()->GetLastCommittedURL());
  EXPECT_EQ(page, web_contents()->GetPrimaryMainFrame()->GetLastCommittedURL());

  EXPECT_TRUE(content::ExecJs(web_contents(),
                              "const a = document.createElement('a');"
                              "a.href = '" +
                                  std::string(chrome::kChromeUIAerieGuardURL) +
                                  "'; document.body.appendChild(a); a.click();"));
  content::RunAllTasksUntilIdle();
  EXPECT_EQ(page, web_contents()->GetLastCommittedURL());
}

// Loaded straight into a tab (no bubble, so no captured tab) the page has no
// chrome.send, and its handler has nothing to act on.
IN_PROC_BROWSER_TEST_F(AerieGuardBrowserTest, TheDirectPageHasNoChromeSend) {
  ASSERT_TRUE(content::NavigateToURL(
      web_contents(), GURL(chrome::kChromeUIAerieGuardURL)));
  EXPECT_EQ(
      "undefined",
      content::EvalJs(web_contents(),
                      "typeof (window.chrome && window.chrome.send)"));
}

// Open, close, navigate, open again: the second panel opens, and the browser
// lives. The controller used to observe the first panel's bubble manager after
// it replaced (and so destroyed) it, and crashed on the second open.
IN_PROC_BROWSER_TEST_F(AerieGuardBrowserTest, TheIconOpensThePanelAgainAfterItClosed) {
  ASSERT_TRUE(content::NavigateToURL(
      web_contents(), embedded_test_server()->GetURL("news.test", "/title1.html")));
  ASSERT_TRUE(guard());

  guard()->ShowPanel(anchor());
  ASSERT_TRUE(guard()->panel_widget_for_testing());
  PressToClose();

  ASSERT_TRUE(content::NavigateToURL(
      web_contents(), embedded_test_server()->GetURL("shop.test", "/title2.html")));
  guard()->ShowPanel(anchor());
  EXPECT_TRUE(guard()->panel_widget_for_testing());
  PressToClose();

  // Once more on the same page.
  guard()->ShowPanel(anchor());
  EXPECT_TRUE(guard()->panel_widget_for_testing());
  PressToClose();
  EXPECT_TRUE(content::ExecJs(web_contents(), "1 + 1"));
}

// A tab closes while its panel is open: the controller goes, and the panel
// with it, without touching the bubble manager it already destroyed.
IN_PROC_BROWSER_TEST_F(AerieGuardBrowserTest, ATabClosesWithItsPanelOpen) {
  ASSERT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
      browser(), embedded_test_server()->GetURL("news.test", "/title1.html"),
      WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
  content::WebContents* tab = web_contents();
  guard()->ShowPanel(anchor());
  views::Widget* panel = guard()->panel_widget_for_testing();
  ASSERT_TRUE(panel);

  views::test::WidgetDestroyedWaiter panel_closed(panel);
  content::WebContentsDestroyedWatcher tab_closed(tab);
  tab->Close();
  tab_closed.Wait();
  panel_closed.Wait();
  EXPECT_TRUE(content::ExecJs(web_contents(), "1 + 1"));
}

// The panel's page, as far as the handler can tell: it keeps the last state the
// handler pushed.
class FakePage : public aerie_guard::mojom::Page {
 public:
  mojo::PendingRemote<aerie_guard::mojom::Page> Bind() {
    return receiver_.BindNewPipeAndPassRemote();
  }
  void OnStateChanged(aerie_guard::mojom::PanelStatePtr state) override {
    last_ = std::move(state);
    ++pushes_;
  }
  const aerie_guard::mojom::PanelStatePtr& last() const { return last_; }
  int pushes() const { return pushes_; }

 private:
  mojo::Receiver<aerie_guard::mojom::Page> receiver_{this};
  aerie_guard::mojom::PanelStatePtr last_;
  int pushes_ = 0;
};

// The fingerprinting row of the panel (issue #102). The tests drive the
// PageHandler through Mojo, as the page does, for a tab the handler captured.
class AerieGuardFingerprintingTest : public AerieGuardBrowserTest {
 public:
  // The panel's handler for |tab|, captured the way the Guard icon captures it.
  void OpenPanelFor(content::WebContents* tab) {
    handler_.reset();
    remote_.reset();
    page_ = std::make_unique<FakePage>();
    handler_ = std::make_unique<aerie_guard::AerieGuardHandler>(
        remote_.BindNewPipeAndPassReceiver(), page_->Bind(),
        aerie_adblock::GuardTarget::Capture(tab),
        /*embedder=*/nullptr);
  }

  // Lets the handler's pushes reach the fake page.
  void Flush() { remote_.FlushForTesting(); base::RunLoop().RunUntilIdle(); }

  aerie_guard::mojom::PanelStatePtr GetState() {
    aerie_guard::mojom::PanelStatePtr out;
    base::RunLoop loop;
    remote_->GetState(base::BindLambdaForTesting(
        [&](aerie_guard::mojom::PanelStatePtr state) {
          out = std::move(state);
          loop.Quit();
        }));
    loop.Run();
    return out;
  }

  GURL Url(const std::string& host) {
    return embedded_test_server()->GetURL(host, "/title1.html");
  }

  // A canvas value a page can read: noise changes it.
  std::string CanvasValue(content::WebContents* tab) {
    return content::EvalJs(tab, R"(
        (() => {
          const c = document.createElement('canvas');
          c.width = 200; c.height = 60;
          const g = c.getContext('2d');
          g.fillStyle = '#f60'; g.fillRect(0, 0, 200, 60);
          g.font = '18px sans-serif'; g.fillStyle = '#069';
          g.fillText('Aerie fingerprint probe 1234567890', 4, 30);
          return c.toDataURL();
        })())").ExtractString();
  }

  void Reload(content::WebContents* tab) {
    content::TestNavigationObserver observer(tab);
    tab->GetController().Reload(content::ReloadType::NORMAL, true);
    observer.Wait();
  }

  ContentSetting NoiseSetting(Profile* profile, const GURL& url) {
    return HostContentSettingsMapFactory::GetForProfile(profile)
        ->GetContentSetting(url, GURL(), ContentSettingsType::HELIUM_NOISE);
  }

  mojo::Remote<aerie_guard::mojom::PageHandler> remote_;
  std::unique_ptr<FakePage> page_;
  std::unique_ptr<aerie_guard::AerieGuardHandler> handler_;
};

// Turning protection off for a.localhost gives its real canvas value back after
// the reload; b.localhost keeps its noise; turning it on again brings the noise
// back.
IN_PROC_BROWSER_TEST_F(AerieGuardFingerprintingTest,
                       TheRowTurnsNoiseOffForOneSiteOnly) {
  // The real value: read with protection off for the whole browser.
  ASSERT_TRUE(content::NavigateToURL(web_contents(), Url("b.localhost")));
  const std::string noised_b = CanvasValue(web_contents());
  chrome_test_utils::GetProfile(this)->GetPrefs()->SetBoolean(prefs::kHeliumNoiseEnabled, false);
  Reload(web_contents());
  const std::string real = CanvasValue(web_contents());
  chrome_test_utils::GetProfile(this)->GetPrefs()->SetBoolean(prefs::kHeliumNoiseEnabled, true);
  EXPECT_NE(noised_b, real);

  ASSERT_TRUE(content::NavigateToURL(web_contents(), Url("a.localhost")));
  const std::string noised_a = CanvasValue(web_contents());
  EXPECT_NE(noised_a, real);

  OpenPanelFor(web_contents());
  aerie_guard::mojom::PanelStatePtr state = GetState();
  ASSERT_TRUE(state);
  EXPECT_EQ(aerie_guard::mojom::FingerprintingMode::kOn,
            state->fingerprinting->mode);
  EXPECT_FALSE(state->fingerprinting->needs_reload);
  EXPECT_FALSE(state->fingerprinting->surfaces.empty());

  remote_->SetFingerprintingOn(false);
  Flush();
  ASSERT_TRUE(page_->last());
  EXPECT_EQ(aerie_guard::mojom::FingerprintingMode::kOffForSite,
            page_->last()->fingerprinting->mode);
  // The tab still shows the old values: the panel asks for a reload.
  EXPECT_TRUE(page_->last()->fingerprinting->needs_reload);
  EXPECT_EQ(noised_a, CanvasValue(web_contents()));

  remote_->Reload();
  Flush();
  content::WaitForLoadStop(web_contents());
  EXPECT_EQ(real, CanvasValue(web_contents()));

  // Another site keeps its noise.
  ASSERT_TRUE(content::NavigateToURL(web_contents(), Url("b.localhost")));
  EXPECT_EQ(noised_b, CanvasValue(web_contents()));

  // And on again.
  ASSERT_TRUE(content::NavigateToURL(web_contents(), Url("a.localhost")));
  OpenPanelFor(web_contents());
  EXPECT_EQ(aerie_guard::mojom::FingerprintingMode::kOffForSite,
            GetState()->fingerprinting->mode);
  remote_->SetFingerprintingOn(true);
  remote_->Reload();
  Flush();
  content::WaitForLoadStop(web_contents());
  EXPECT_EQ(noised_a, CanvasValue(web_contents()));
}

// With protection off in Settings the row has no switch, and a call from the
// page changes nothing.
IN_PROC_BROWSER_TEST_F(AerieGuardFingerprintingTest,
                       OffInSettingsHasNoSwitchAndRefusesTheCall) {
  chrome_test_utils::GetProfile(this)->GetPrefs()->SetBoolean(prefs::kHeliumNoiseEnabled, false);
  ASSERT_TRUE(content::NavigateToURL(web_contents(), Url("a.localhost")));
  OpenPanelFor(web_contents());
  EXPECT_EQ(aerie_guard::mojom::FingerprintingMode::kOffInSettings,
            GetState()->fingerprinting->mode);

  remote_->SetFingerprintingOn(false);
  Flush();
  EXPECT_EQ(CONTENT_SETTING_ALLOW,
            NoiseSetting(chrome_test_utils::GetProfile(this), Url("a.localhost")));
  EXPECT_EQ(aerie_guard::mojom::FingerprintingMode::kOffInSettings,
            page_->last()->fingerprinting->mode);
}

// The handler acts on the host it captured. After the tab went to another
// site it refuses, and no setting changes for either site.
IN_PROC_BROWSER_TEST_F(AerieGuardFingerprintingTest,
                       AStaleTargetIsRefusedAndChangesNothing) {
  ASSERT_TRUE(content::NavigateToURL(web_contents(), Url("a.localhost")));
  OpenPanelFor(web_contents());
  ASSERT_TRUE(GetState());
  ASSERT_TRUE(content::NavigateToURL(web_contents(), Url("b.localhost")));

  remote_->SetFingerprintingOn(false);
  Flush();
  EXPECT_EQ(CONTENT_SETTING_ALLOW,
            NoiseSetting(chrome_test_utils::GetProfile(this), Url("a.localhost")));
  EXPECT_EQ(CONTENT_SETTING_ALLOW,
            NoiseSetting(chrome_test_utils::GetProfile(this), Url("b.localhost")));
  EXPECT_FALSE(GetState());
}

// A panel with no captured tab changes nothing.
IN_PROC_BROWSER_TEST_F(AerieGuardFingerprintingTest,
                       APanelWithNoTargetChangesNothing) {
  ASSERT_TRUE(content::NavigateToURL(web_contents(), Url("a.localhost")));
  OpenPanelFor(nullptr);
  remote_->SetFingerprintingOn(false);
  Flush();
  EXPECT_EQ(CONTENT_SETTING_ALLOW,
            NoiseSetting(chrome_test_utils::GetProfile(this), Url("a.localhost")));
}

// In an incognito window the row writes to the incognito map only. The
// regular profile and its saved preferences do not change.
IN_PROC_BROWSER_TEST_F(AerieGuardFingerprintingTest,
                       InIncognitoTheRowChangesOnlyTheIncognitoMap) {
  Profile* regular = chrome_test_utils::GetProfile(this);
  const base::DictValue before =
      regular->GetPrefs()
          ->GetDict("profile.content_settings.exceptions.helium_noise")
          .Clone();

  BrowserWindowInterface* incognito = CreateIncognitoBrowser();
  Profile* otr = incognito->GetProfile();
  ASSERT_TRUE(otr->IsOffTheRecord());
  ASSERT_TRUE(content::NavigateToURL(
      incognito->GetActiveTabInterface()->GetContents(), Url("a.localhost")));
  OpenPanelFor(incognito->GetActiveTabInterface()->GetContents());
  aerie_guard::mojom::PanelStatePtr state = GetState();
  ASSERT_TRUE(state);
  EXPECT_TRUE(state->incognito);

  remote_->SetFingerprintingOn(false);
  Flush();
  EXPECT_EQ(aerie_guard::mojom::FingerprintingMode::kOffForSite,
            page_->last()->fingerprinting->mode);
  EXPECT_EQ(CONTENT_SETTING_BLOCK, NoiseSetting(otr, Url("a.localhost")));
  EXPECT_EQ(CONTENT_SETTING_ALLOW, NoiseSetting(regular, Url("a.localhost")));
  EXPECT_EQ(before,
            regular->GetPrefs()->GetDict(
                "profile.content_settings.exceptions.helium_noise"));
}

}  // namespace
