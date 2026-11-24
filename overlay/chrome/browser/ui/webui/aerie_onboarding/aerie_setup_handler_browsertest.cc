// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

// Browser tests of what aerie://setup may ask the browser to do (issue #69). The page
// can mark setup complete and accept the schema, and nothing else: no other pref, and
// no extension messages.

#include "chrome/browser/ui/webui/aerie_onboarding/aerie_setup_handler.h"

#include <memory>
#include <string>

#include "base/functional/bind.h"
#include "base/test/run_until.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "components/helium_services/pref_names.h"
#include "components/prefs/pref_service.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/test_web_ui.h"

namespace aerie_onboarding {

class AerieSetupHandlerBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    web_ui_ = std::make_unique<content::TestWebUI>();
    web_ui_->set_web_contents(
        browser()->tab_strip_model()->GetActiveWebContents());
    handler_ = std::make_unique<AerieSetupHandler>(prefs());
    handler_->set_web_ui(web_ui_.get());
    handler_->RegisterMessages();
  }

  void TearDownOnMainThread() override {
    handler_->set_web_ui(nullptr);
    handler_.reset();
    web_ui_.reset();
    InProcessBrowserTest::TearDownOnMainThread();
  }

  PrefService* prefs() { return browser()->GetProfile()->GetPrefs(); }

  // Sends `name` with a callback id and `args`, and returns how many answers
  // the page got by the time the message was handled.
  size_t Send(const std::string& name, base::ListValue args = {}) {
    base::ListValue list;
    list.Append("cb" + std::to_string(++calls_));
    for (base::Value& arg : args) {
      list.Append(std::move(arg));
    }
    web_ui_->HandleReceivedMessage(name, list);
    return web_ui_->call_data().size();
  }

  // Whether the last answer was a rejection.
  bool LastRejected() {
    const auto& call = web_ui_->call_data().back();
    return call->function_name() == "cr.webUIResponse" && call->arg2() &&
           call->arg2()->is_bool() && !call->arg2()->GetBool();
  }

  std::unique_ptr<content::TestWebUI> web_ui_;
  std::unique_ptr<AerieSetupHandler> handler_;
  int calls_ = 0;
};

IN_PROC_BROWSER_TEST_F(AerieSetupHandlerBrowserTest, CompletedOnboardingCanBeSet) {
  prefs()->SetBoolean(::prefs::kHeliumDidOnboarding, false);
  base::ListValue args;
  args.Append("completed_onboarding");
  args.Append(true);
  Send("setPref", std::move(args));
  EXPECT_TRUE(prefs()->GetBoolean(::prefs::kHeliumDidOnboarding));
  EXPECT_FALSE(LastRejected());
}

IN_PROC_BROWSER_TEST_F(AerieSetupHandlerBrowserTest, ConsentToHeliumServicesCannotBeSet) {
  prefs()->SetBoolean(::prefs::kHeliumServicesConsented, false);
  base::ListValue args;
  args.Append("services.user_consented");
  args.Append(true);
  Send("setPref", std::move(args));
  EXPECT_FALSE(prefs()->GetBoolean(::prefs::kHeliumServicesConsented));
  EXPECT_TRUE(LastRejected());
}

IN_PROC_BROWSER_TEST_F(AerieSetupHandlerBrowserTest, OtherHeliumPrefsCannotBeSet) {
  prefs()->SetBoolean(::prefs::kHeliumServicesEnabled, false);
  base::ListValue args;
  args.Append("services.enabled");
  args.Append(true);
  Send("setPref", std::move(args));
  EXPECT_FALSE(prefs()->GetBoolean(::prefs::kHeliumServicesEnabled));
  EXPECT_TRUE(LastRejected());
}

IN_PROC_BROWSER_TEST_F(AerieSetupHandlerBrowserTest, CompletedOnboardingNeedsABoolean) {
  prefs()->SetBoolean(::prefs::kHeliumDidOnboarding, false);
  base::ListValue args;
  args.Append("completed_onboarding");
  args.Append("yes");
  Send("setPref", std::move(args));
  EXPECT_FALSE(prefs()->GetBoolean(::prefs::kHeliumDidOnboarding));
  EXPECT_TRUE(LastRejected());
}

IN_PROC_BROWSER_TEST_F(AerieSetupHandlerBrowserTest, MalformedSetPrefDoesNotCrash) {
  Send("setPref");
  base::ListValue args;
  args.Append(42);
  args.Append(true);
  Send("setPref", std::move(args));
  EXPECT_TRUE(LastRejected());
}

IN_PROC_BROWSER_TEST_F(AerieSetupHandlerBrowserTest, ExtensionMessagesHaveNoHandler) {
  // setPref answers, so the callback map works; the extension messages are not in it.
  base::ListValue probe;
  probe.Append("completed_onboarding");
  probe.Append(true);
  EXPECT_EQ(Send("setPref", std::move(probe)), 1u);
  base::ListValue install;
  install.Append("some-extension-id");
  EXPECT_EQ(Send("installExtension", std::move(install)), 1u);
  EXPECT_EQ(Send("getExtensions"), 1u);
}

IN_PROC_BROWSER_TEST_F(AerieSetupHandlerBrowserTest, AcceptLatestSchemaStillWorks) {
  prefs()->SetInteger(::prefs::kHeliumSchemaVersion, 0);
  Send("acceptLatestSchema");
  EXPECT_GT(prefs()->GetInteger(::prefs::kHeliumSchemaVersion), 0);
}

}  // namespace aerie_onboarding
