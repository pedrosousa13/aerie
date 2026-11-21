// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

// Browser tests of automatic picture-in-picture (issue #263). Aerie turns on
// what Chromium has (AutoPictureInPictureTabHelper): a video that plays enters
// picture-in-picture when the user switches to another tab. The tests check
// that it enters, that the Settings switch and a per-site exception stop it,
// and that a page cannot start it without the user's switch. Run with
// ./aerie test-cpp --target aerie_browser_tests.

#include <memory>
#include <string>

#include "base/feature_list.h"
#include "base/run_loop.h"
#include "base/strings/strcat.h"
#include "base/task/single_thread_task_runner.h"
#include "base/test/run_until.h"
#include "base/time/time.h"
#include "chrome/browser/aerie_picture_in_picture/aerie_auto_pip_fixture.h"
#include "chrome/browser/content_settings/host_content_settings_map_factory.h"
#include "chrome/browser/picture_in_picture/auto_picture_in_picture_tab_helper.h"
#include "chrome/browser/picture_in_picture/picture_in_picture_window_manager.h"
#include "chrome/browser/ui/recently_audible_helper.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"
#include "components/content_settings/core/common/content_settings.h"
#include "components/content_settings/core/common/content_settings_types.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "third_party/blink/public/common/features.h"
#include "url/gurl.h"

namespace {

// A page with one video that has a picture and a quiet tone, from the file in
// aerie_auto_pip_fixture.h. It sets no media session action: the browser
// decides, as it does for most sites. Nothing plays until the test calls
// startVideo() (a call from the test counts as a user gesture).
std::string VideoPage() {
  return base::StrCat(
      {"<!doctype html>\n<title>Video Fixture</title>\n<body>\n"
       "<video id=video loop width=320 height=180 "
       "src=\"data:video/webm;base64,",
       kAerieAutoPipWebmBase64,
       "\"></video>\n<script>\n"
       "async function startVideo() { await video.play(); }\n"
       "async function askForPictureInPicture() {\n"
       "  if (video.readyState < 1) {\n"
       "    await new Promise(r => video.addEventListener('loadedmetadata', r));\n"
       "  }\n"
       "  try { await video.requestPictureInPicture(); return 'entered'; }\n"
       "  catch (e) { return e.name; }\n"
       "}\n</script>\n"});
}

std::unique_ptr<net::test_server::HttpResponse> ServeVideoPage(
    const net::test_server::HttpRequest& request) {
  if (request.relative_url != "/video.html") {
    return nullptr;
  }
  auto response = std::make_unique<net::test_server::BasicHttpResponse>();
  response->set_content_type("text/html");
  response->set_content(VideoPage());
  return response;
}

}  // namespace

class AerieAutoPipBrowserTest : public InProcessBrowserTest {
 public:
  AerieAutoPipBrowserTest()
      : https_server_(net::EmbeddedTestServer::TYPE_HTTPS) {}

 protected:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    host_resolver()->AddRule("*", "127.0.0.1");
    https_server_.RegisterRequestHandler(base::BindRepeating(&ServeVideoPage));
    ASSERT_TRUE(https_server_.Start());
  }
  void TearDownOnMainThread() override {
    PictureInPictureWindowManager::GetInstance()->ExitPictureInPicture();
    InProcessBrowserTest::TearDownOnMainThread();
  }

  GURL VideoUrl() { return https_server_.GetURL("/video.html"); }
  HostContentSettingsMap* settings() {
    return HostContentSettingsMapFactory::GetForProfile(browser()->GetProfile());
  }
  content::WebContents* active() {
    return browser()->tab_strip_model()->GetActiveWebContents();
  }

  // Opens the video page in a new tab, shows it, and starts the video.
  content::WebContents* PlayVideoInANewTab() {
    EXPECT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
        browser(), VideoUrl(), WindowOpenDisposition::NEW_FOREGROUND_TAB,
        ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
    content::WebContents* tab = active();
    EXPECT_TRUE(content::ExecJs(tab, "startVideo()"));
    WaitUntilTheBrowserWatchesTheVideo(tab);
    return tab;
  }
  // A person needs a moment to click another tab. The test must wait for what
  // that moment gives the browser: the session says it may enter
  // picture-in-picture (which is what makes the tab helper watch the tab
  // strip), and the video has audio focus and is audible.
  void WaitUntilTheBrowserWatchesTheVideo(content::WebContents* tab) {
    auto* helper = AutoPictureInPictureTabHelper::FromWebContents(tab);
    ASSERT_TRUE(helper);
    ASSERT_TRUE(base::test::RunUntil([&] {
      return helper->HasAutoPictureInPictureBeenRegistered() &&
             IsAudiblyPlaying(helper);
    }));
  }
  static bool IsAudiblyPlaying(const AutoPictureInPictureTabHelper* helper) {
    const auto info = helper->GetAutoPipInfo();
    return info.has_audio_focus && info.is_playing && info.was_recently_audible;
  }
  // The user clicks another tab: the first tab of the window.
  void SwitchAwayFrom(content::WebContents* tab) {
    ASSERT_EQ(active(), tab);
    browser()->tab_strip_model()->ActivateTabAt(0);
    ASSERT_NE(active(), tab);
  }
  void SwitchBackTo(content::WebContents* tab) {
    browser()->tab_strip_model()->ActivateTabAt(
        browser()->tab_strip_model()->GetIndexOfWebContents(tab));
    ASSERT_EQ(active(), tab);
  }

  bool InPictureInPicture(content::WebContents* tab) {
    return PictureInPictureWindowManager::GetInstance()->GetWebContents() ==
           tab;
  }
  void WaitForPictureInPicture(content::WebContents* tab) {
    ASSERT_TRUE(base::test::RunUntil([&] { return InPictureInPicture(tab); }));
  }
  // Gives the browser time to enter picture-in-picture, for the tests that say
  // it must not. A positive control in the same test shows that the wait is
  // long enough to notice an entry.
  void WaitAWhile() {
    base::RunLoop run_loop;
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, run_loop.QuitClosure(), base::Seconds(2));
    run_loop.Run();
  }

 private:
  net::EmbeddedTestServer https_server_;
};

// The Settings switch is on out of the box (the opt-out rule), and so is the
// browser's own entry for a page that sets no handler.
IN_PROC_BROWSER_TEST_F(AerieAutoPipBrowserTest, IsOnByDefault) {
  EXPECT_EQ(settings()->GetDefaultContentSetting(
                ContentSettingsType::AUTO_PICTURE_IN_PICTURE),
            CONTENT_SETTING_ALLOW);
  EXPECT_TRUE(base::FeatureList::IsEnabled(
      blink::features::kBrowserInitiatedAutomaticPictureInPicture));
}

IN_PROC_BROWSER_TEST_F(AerieAutoPipBrowserTest,
                       EntersWhenTheUserSwitchesAwayFromAPlayingVideo) {
  content::WebContents* tab = PlayVideoInANewTab();
  EXPECT_FALSE(InPictureInPicture(tab));
  SwitchAwayFrom(tab);
  WaitForPictureInPicture(tab);
}

IN_PROC_BROWSER_TEST_F(AerieAutoPipBrowserTest,
                       DoesNotEnterWhenTheSettingIsOff) {
  settings()->SetDefaultContentSetting(
      ContentSettingsType::AUTO_PICTURE_IN_PICTURE, CONTENT_SETTING_BLOCK);
  content::WebContents* tab = PlayVideoInANewTab();
  SwitchAwayFrom(tab);
  WaitAWhile();
  EXPECT_FALSE(InPictureInPicture(tab));

  // The same steps with the switch on: it enters, so the wait above was long
  // enough to notice an entry.
  settings()->SetDefaultContentSetting(
      ContentSettingsType::AUTO_PICTURE_IN_PICTURE, CONTENT_SETTING_ALLOW);
  SwitchBackTo(tab);
  SwitchAwayFrom(tab);
  WaitForPictureInPicture(tab);
}

IN_PROC_BROWSER_TEST_F(AerieAutoPipBrowserTest,
                       DoesNotEnterForASiteInTheExceptionsList) {
  const GURL url = VideoUrl();
  settings()->SetContentSettingDefaultScope(
      url, url, ContentSettingsType::AUTO_PICTURE_IN_PICTURE,
      CONTENT_SETTING_BLOCK);
  content::WebContents* tab = PlayVideoInANewTab();
  SwitchAwayFrom(tab);
  WaitAWhile();
  EXPECT_FALSE(InPictureInPicture(tab));

  // Removing the exception brings it back.
  settings()->SetContentSettingDefaultScope(
      url, url, ContentSettingsType::AUTO_PICTURE_IN_PICTURE,
      CONTENT_SETTING_DEFAULT);
  SwitchBackTo(tab);
  SwitchAwayFrom(tab);
  WaitForPictureInPicture(tab);
}

// A page cannot start it: a page that asks without a click is refused, and the
// video does not enter while its tab stays in front. Only the user's switch
// does it.
IN_PROC_BROWSER_TEST_F(AerieAutoPipBrowserTest,
                       APageCannotStartItWithoutTheUsersSwitch) {
  ASSERT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
      browser(), VideoUrl(), WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
  content::WebContents* tab = active();
  // The page asks for picture-in-picture itself, with no click: refused.
  EXPECT_EQ(content::EvalJs(tab, "askForPictureInPicture()",
                            content::EXECUTE_SCRIPT_NO_USER_GESTURE)
                .ExtractString(),
            "NotAllowedError");
  EXPECT_FALSE(InPictureInPicture(tab));

  // The video plays in front for a while: nothing enters.
  ASSERT_TRUE(content::ExecJs(tab, "startVideo()"));
  WaitUntilTheBrowserWatchesTheVideo(tab);
  WaitAWhile();
  EXPECT_FALSE(InPictureInPicture(tab));

  // Control: the user's own switch does it.
  SwitchAwayFrom(tab);
  WaitForPictureInPicture(tab);
}
