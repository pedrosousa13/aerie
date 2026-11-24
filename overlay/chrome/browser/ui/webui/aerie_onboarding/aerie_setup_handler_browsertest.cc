// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

// Browser tests of what aerie://setup may ask the browser to do (issue #69).
// The page can mark setup complete and accept the schema, and nothing else: no
// other pref, and no extension messages. The handler is driven directly, and
// the real page is driven through its own chrome.send.

#include "chrome/browser/ui/webui/aerie_onboarding/aerie_setup_handler.h"

#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <utility>

#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "components/helium_services/pref_names.h"
#include "components/prefs/pref_service.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/test_web_ui.h"
#include "url/gurl.h"

namespace aerie_onboarding {

namespace {

// A TestWebUI that remembers every message name a handler registers.
class RecordingTestWebUI : public content::TestWebUI {
 public:
  void RegisterMessageCallback(std::string_view message,
                               MessageCallback callback) override {
    registered_.insert(std::string(message));
    content::TestWebUI::RegisterMessageCallback(message, std::move(callback));
  }

  const std::set<std::string>& registered() const { return registered_; }

 private:
  std::set<std::string> registered_;
};

// Sends a message from the page the way setup.js does (chrome.send with a
// callback id) and answers "ok" or "rejected" when the page gets the response.
constexpr char kPageSend[] = R"(((name, ...args) => new Promise(resolve => {
  const id = 'test_' + name + '_' +
      (window.testCalls = (window.testCalls || 0) + 1);
  const previous = window.cr.webUIResponse;
  window.cr.webUIResponse = (responseId, ok, response) => {
    if (responseId !== id) {
      return previous(responseId, ok, response);
    }
    window.cr.webUIResponse = previous;
    resolve(ok ? 'ok' : 'rejected');
  };
  chrome.send(name, [id, ...args]);
})))";

}  // namespace

class AerieSetupHandlerBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    web_ui_ = std::make_unique<RecordingTestWebUI>();
    web_ui_->set_web_contents(
        browser()->tab_strip_model()->GetActiveWebContents());
    // The TestWebUI sets the handler's WebUI and registers its messages.
    web_ui_->AddMessageHandler(std::make_unique<AerieSetupHandler>(prefs()));
  }

  void TearDownOnMainThread() override {
    web_ui_.reset();
    InProcessBrowserTest::TearDownOnMainThread();
  }

  PrefService* prefs() { return browser()->GetProfile()->GetPrefs(); }

  // Sends `setPref` for `name` and `value`, as the page would.
  void SendSetPref(const std::string& name, base::Value value) {
    base::ListValue args;
    args.Append("cb" + std::to_string(++calls_));
    args.Append(name);
    args.Append(std::move(value));
    web_ui_->HandleReceivedMessage("setPref", args);
  }

  // How many answers the page has got in total since the test began.
  size_t TotalAnswers() { return web_ui_->call_data().size(); }

  // Whether the latest answer was a rejection.
  bool LastRejected() {
    const auto& call = web_ui_->call_data().back();
    return call->function_name() == "cr.webUIResponse" && call->arg2() &&
           call->arg2()->is_bool() && !call->arg2()->GetBool();
  }

  std::unique_ptr<RecordingTestWebUI> web_ui_;
  int calls_ = 0;
};

IN_PROC_BROWSER_TEST_F(AerieSetupHandlerBrowserTest,
                       CompletedOnboardingCanBeSet) {
  prefs()->SetBoolean(::prefs::kHeliumDidOnboarding, false);
  SendSetPref("completed_onboarding", base::Value(true));
  EXPECT_TRUE(prefs()->GetBoolean(::prefs::kHeliumDidOnboarding));
  EXPECT_FALSE(LastRejected());
}

IN_PROC_BROWSER_TEST_F(AerieSetupHandlerBrowserTest,
                       ConsentToHeliumServicesCannotBeSet) {
  prefs()->SetBoolean(::prefs::kHeliumServicesConsented, false);
  SendSetPref("services.user_consented", base::Value(true));
  EXPECT_FALSE(prefs()->GetBoolean(::prefs::kHeliumServicesConsented));
  EXPECT_TRUE(LastRejected());
}

IN_PROC_BROWSER_TEST_F(AerieSetupHandlerBrowserTest,
                       OtherHeliumPrefsCannotBeSet) {
  prefs()->SetBoolean(::prefs::kHeliumServicesEnabled, false);
  SendSetPref("services.enabled", base::Value(true));
  EXPECT_FALSE(prefs()->GetBoolean(::prefs::kHeliumServicesEnabled));
  EXPECT_TRUE(LastRejected());
}

IN_PROC_BROWSER_TEST_F(AerieSetupHandlerBrowserTest,
                       FullyPrefixedNamesAreRejected) {
  prefs()->SetBoolean(::prefs::kHeliumDidOnboarding, false);
  SendSetPref("helium.completed_onboarding", base::Value(true));
  EXPECT_FALSE(prefs()->GetBoolean(::prefs::kHeliumDidOnboarding));
  EXPECT_TRUE(LastRejected());

  prefs()->SetBoolean(::prefs::kHeliumServicesConsented, false);
  SendSetPref("helium.services.user_consented", base::Value(true));
  EXPECT_FALSE(prefs()->GetBoolean(::prefs::kHeliumServicesConsented));
  EXPECT_TRUE(LastRejected());
}

IN_PROC_BROWSER_TEST_F(AerieSetupHandlerBrowserTest,
                       CompletedOnboardingNeedsABoolean) {
  prefs()->SetBoolean(::prefs::kHeliumDidOnboarding, false);
  SendSetPref("completed_onboarding", base::Value("yes"));
  EXPECT_FALSE(prefs()->GetBoolean(::prefs::kHeliumDidOnboarding));
  EXPECT_TRUE(LastRejected());
}

IN_PROC_BROWSER_TEST_F(AerieSetupHandlerBrowserTest,
                       MalformedSetPrefDoesNotCrash) {
  base::ListValue only_id;
  only_id.Append("cb0");
  web_ui_->HandleReceivedMessage("setPref", only_id);
  EXPECT_TRUE(LastRejected());

  SendSetPref("", base::Value(true));
  EXPECT_TRUE(LastRejected());
}

IN_PROC_BROWSER_TEST_F(AerieSetupHandlerBrowserTest,
                       OnlySetPrefAndAcceptLatestSchemaAreRegistered) {
  EXPECT_EQ(web_ui_->registered(),
            (std::set<std::string>{"setPref", "acceptLatestSchema"}));
  EXPECT_FALSE(web_ui_->registered().contains("installExtension"));
  EXPECT_FALSE(web_ui_->registered().contains("getExtensions"));
}

IN_PROC_BROWSER_TEST_F(AerieSetupHandlerBrowserTest,
                       ExtensionMessagesGetNoAnswer) {
  const size_t before = TotalAnswers();
  base::ListValue install;
  install.Append("cb");
  install.Append("some-extension-id");
  web_ui_->HandleReceivedMessage("installExtension", install);
  base::ListValue get;
  get.Append("cb");
  web_ui_->HandleReceivedMessage("getExtensions", get);
  EXPECT_EQ(TotalAnswers(), before);
}

IN_PROC_BROWSER_TEST_F(AerieSetupHandlerBrowserTest,
                       AcceptLatestSchemaStillWorks) {
  prefs()->SetInteger(::prefs::kHeliumSchemaVersion, 0);
  web_ui_->HandleReceivedMessage("acceptLatestSchema", base::ListValue());
  EXPECT_GT(prefs()->GetInteger(::prefs::kHeliumSchemaVersion), 0);
}

// The real aerie://setup page, with the handlers the patch registers.
IN_PROC_BROWSER_TEST_F(AerieSetupHandlerBrowserTest,
                       TheRealSetupPageCompletesButCannotConsent) {
  prefs()->SetBoolean(::prefs::kHeliumDidOnboarding, false);
  prefs()->SetBoolean(::prefs::kHeliumServicesConsented, false);
  content::WebContents* contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(content::NavigateToURL(contents, GURL("chrome://setup/")));
  const std::string send = kPageSend;

  EXPECT_EQ("rejected",
            content::EvalJs(
                contents, send + "('setPref', 'services.user_consented', true)"));
  EXPECT_FALSE(prefs()->GetBoolean(::prefs::kHeliumServicesConsented));
  EXPECT_EQ("rejected",
            content::EvalJs(contents,
                            send + "('setPref', "
                                   "'helium.services.user_consented', true)"));
  EXPECT_FALSE(prefs()->GetBoolean(::prefs::kHeliumServicesConsented));
  EXPECT_FALSE(prefs()->GetBoolean(::prefs::kHeliumDidOnboarding));

  EXPECT_EQ("ok",
            content::EvalJs(
                contents, send + "('setPref', 'completed_onboarding', true)"));
  EXPECT_TRUE(prefs()->GetBoolean(::prefs::kHeliumDidOnboarding));
  EXPECT_FALSE(prefs()->GetBoolean(::prefs::kHeliumServicesConsented));
}

}  // namespace aerie_onboarding
