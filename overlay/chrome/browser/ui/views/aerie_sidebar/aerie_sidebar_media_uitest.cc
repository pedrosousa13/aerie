// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

// Interactive UI tests of the media card at the bottom of the sidebar (issue
// #263): it shows the title and site of the tab that plays, Play or Pause
// controls the page, Next is wired to the page's "nexttrack" handler and is
// enabled only when the page set one, Go to tab activates the tab, the card
// goes when the media stops or the tab closes, and its show and hide follow
// reduced motion. Run with ./aerie test-cpp --target aerie_interactive_ui_tests.

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "base/strings/string_util.h"
#include "base/test/run_until.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/aerie_picture_in_picture/aerie_auto_pip_fixture.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_media_card.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_media_controller.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_view.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/interactive_test_utils.h"
#include "chrome/test/base/ui_test_utils.h"
#include "base/command_line.h"
#include "media/base/media_switches.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/layer_animation_element.h"
#include "ui/compositor/layer_animator.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/animation/animation_test_api.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"

namespace {

// A page that plays media. The sound is the seven second WebM of the
// picture-in-picture test, inline as a data URL and looped, so the fixture needs
// no file and no network. Nothing plays until the test calls audio.play() (a call
// from the test counts as a user gesture). The page's own script can also:
//   setTitle(text)  give the media session a track title,
//   enableNext()    handle the "nexttrack" action (counted in nextCount),
//   stopMedia()     end the media (the player goes away).
constexpr char kMediaPageTemplate[] = R"(<!doctype html>
<title>Fixture Song</title>
<body>
<video id=audio loop width=64 height=48 src="data:video/webm;base64,%BASE64%"></video>
<script>
const audio = document.getElementById('audio');
let nextCount = 0;
function setTitle(title) {
  navigator.mediaSession.metadata = new MediaMetadata({title: title});
}
function enableNext() {
  navigator.mediaSession.setActionHandler('nexttrack', () => { ++nextCount; });
}
function stopMedia() {
  audio.pause();
  audio.removeAttribute('src');
  audio.load();
}
</script>
)";

std::string MediaPage() {
  std::string page = kMediaPageTemplate;
  page.replace(page.find("%BASE64%"), 8, kAerieAutoPipWebmBase64);
  return page;
}

std::unique_ptr<net::test_server::HttpResponse> ServeMediaPage(
    const net::test_server::HttpRequest& request) {
  if (request.relative_url != "/media.html") {
    return nullptr;
  }
  auto response = std::make_unique<net::test_server::BasicHttpResponse>();
  response->set_content_type("text/html");
  response->set_content(MediaPage());
  return response;
}

template <typename T>
T* FindDescendant(views::View* root) {
  if (auto* match = views::AsViewClass<T>(root)) {
    return match;
  }
  for (views::View* child : root->children()) {
    if (T* found = FindDescendant<T>(child)) {
      return found;
    }
  }
  return nullptr;
}

std::u16string NameOf(views::View* view) {
  ui::AXNodeData data;
  view->GetViewAccessibility().GetAccessibleNodeData(&data);
  return data.GetString16Attribute(ax::mojom::StringAttribute::kName);
}

}  // namespace

class AerieSidebarMediaUiTest : public InProcessBrowserTest {
 protected:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    InProcessBrowserTest::SetUpCommandLine(command_line);
    // A test machine may have no sound card; the media then waits for one and
    // never starts. The null sink plays to nowhere and the session is the same.
    command_line->AppendSwitch(switches::kDisableAudioOutput);
  }
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));
    host_resolver()->AddRule("*", "127.0.0.1");
    embedded_test_server()->RegisterRequestHandler(
        base::BindRepeating(&ServeMediaPage));
    ASSERT_TRUE(embedded_test_server()->Start());
    ASSERT_TRUE(base::test::RunUntil([&] { return sidebar() != nullptr; }));
  }
  void TearDownOnMainThread() override {
    reduced_motion_.reset();
    InProcessBrowserTest::TearDownOnMainThread();
  }

  void ForceReducedMotion(bool reduced) {
    reduced_motion_.reset();
    reduced_motion_.emplace(
        gfx::AnimationTestApi::SetPrefersReducedMotionForTesting(reduced));
  }

  BrowserView* browser_view() {
    return BrowserView::GetBrowserViewForBrowser(browser());
  }
  AerieSidebarView* sidebar() {
    return FindDescendant<AerieSidebarView>(
        browser_view()->vertical_tab_strip_region_view_for_testing());
  }
  AerieSidebarMediaCard* card() { return sidebar()->media_card(); }
  views::View* focused() {
    return browser_view()->GetWidget()->GetFocusManager()->GetFocusedView();
  }

  // Opens the media page in a new tab that comes to the front, and returns it.
  // A tab that is never shown does not load its media on a test machine, so a
  // test that needs the tab behind another one first plays in front and then
  // calls ShowTheFirstTab().
  content::WebContents* OpenMediaTab() {
    EXPECT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
        browser(), embedded_test_server()->GetURL("localhost", "/media.html"),
        WindowOpenDisposition::NEW_FOREGROUND_TAB,
        ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
    return browser()->tab_strip_model()->GetWebContentsAt(
        browser()->tab_strip_model()->count() - 1);
  }
  void Play(content::WebContents* contents) {
    // The promise of play() waits for an audio device, which a test machine may
    // lack, so the call does not wait for it.
    ASSERT_TRUE(content::ExecJs(contents, "audio.play(); 0",
                                content::EXECUTE_SCRIPT_NO_RESOLVE_PROMISES));
  }
  bool Paused(content::WebContents* contents) {
    return content::EvalJs(contents, "audio.paused").ExtractBool();
  }
  void ShowTheFirstTab() {
    TabStripModel* model = browser()->tab_strip_model();
    model->ActivateTabAt(0);
    ASSERT_TRUE(base::test::RunUntil([&] { return model->active_index() == 0; }));
  }
  void WaitForCard() {
    ASSERT_TRUE(base::test::RunUntil([&] { return card()->shown(); }));
  }
  void WaitForNoCard() {
    ASSERT_TRUE(base::test::RunUntil([&] { return !card()->shown(); }));
  }
  void PressWithTheKeyboard(views::View* button) {
    button->RequestFocus();
    ASSERT_TRUE(base::test::RunUntil([&] { return focused() == button; }));
    ASSERT_TRUE(ui_test_utils::SendKeyPressSync(browser(), ui::VKEY_RETURN,
                                                false, false, false, false));
  }

 private:
  std::optional<gfx::AnimationTestApi::PrefersReducedMotionResetter>
      reduced_motion_;
};

IN_PROC_BROWSER_TEST_F(AerieSidebarMediaUiTest, NoCardWhileNothingPlays) {
  EXPECT_FALSE(card()->shown());
  OpenMediaTab();  // Loaded, not playing.
  EXPECT_FALSE(card()->shown());
}

IN_PROC_BROWSER_TEST_F(AerieSidebarMediaUiTest,
                       TheCardShowsTheTitleAndSiteOfTheTabThatPlays) {
  ForceReducedMotion(true);
  Play(OpenMediaTab());
  WaitForCard();
  // The page set no track title, so the card says the tab's title.
  base::test::RunUntil(
      [&] { return card()->title_label()->GetText() == u"Fixture Song"; });
  EXPECT_EQ(card()->title_label()->GetText(), u"Fixture Song");
  EXPECT_EQ(card()->site_label()->GetText(), u"localhost");
  EXPECT_EQ(NameOf(card()), u"Now playing");
  // The card is the last thing above the bottom bar.
  EXPECT_LT(card()->y(), sidebar()->bottom_bar()->y());
  EXPECT_LE(card()->bounds().bottom(), sidebar()->bottom_bar()->y());
}

IN_PROC_BROWSER_TEST_F(AerieSidebarMediaUiTest, ThePagesTrackTitleWins) {
  ForceReducedMotion(true);
  content::WebContents* tab = OpenMediaTab();
  ASSERT_TRUE(content::ExecJs(tab, "setTitle('Track One')"));
  Play(tab);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return card()->title_label()->GetText() == u"Track One"; }));
}

IN_PROC_BROWSER_TEST_F(AerieSidebarMediaUiTest, PlayPauseTogglesThePlayback) {
  ForceReducedMotion(true);
  content::WebContents* tab = OpenMediaTab();
  Play(tab);
  WaitForCard();
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return NameOf(card()->play_pause_button()) == u"Pause"; }));
  ASSERT_FALSE(Paused(tab));

  PressWithTheKeyboard(card()->play_pause_button());
  ASSERT_TRUE(base::test::RunUntil([&] { return Paused(tab); }));
  // A paused tab keeps the card, and the button says Play.
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return NameOf(card()->play_pause_button()) == u"Play"; }));
  EXPECT_TRUE(card()->shown());

  PressWithTheKeyboard(card()->play_pause_button());
  ASSERT_TRUE(base::test::RunUntil([&] { return !Paused(tab); }));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return NameOf(card()->play_pause_button()) == u"Pause"; }));
}

IN_PROC_BROWSER_TEST_F(AerieSidebarMediaUiTest,
                       NextIsWiredToThePagesNextTrackAction) {
  ForceReducedMotion(true);
  content::WebContents* tab = OpenMediaTab();
  Play(tab);
  WaitForCard();
  // The page has no handler: Next is there but disabled.
  EXPECT_FALSE(card()->next_button()->GetEnabled());
  EXPECT_EQ(NameOf(card()->next_button()), u"Next track");

  ASSERT_TRUE(content::ExecJs(tab, "enableNext()"));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return card()->next_button()->GetEnabled(); }));
  PressWithTheKeyboard(card()->next_button());
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return content::EvalJs(tab, "nextCount").ExtractInt() == 1; }));
}

IN_PROC_BROWSER_TEST_F(AerieSidebarMediaUiTest, GoToTabActivatesThePlayingTab) {
  ForceReducedMotion(true);
  content::WebContents* tab = OpenMediaTab();
  Play(tab);
  WaitForCard();
  ShowTheFirstTab();
  TabStripModel* model = browser()->tab_strip_model();
  ASSERT_EQ(model->active_index(), 0);
  PressWithTheKeyboard(card()->go_to_tab_button());
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return model->GetActiveWebContents() == tab; }));
  // The tab stays on the card while it plays.
  EXPECT_TRUE(card()->shown());
}

IN_PROC_BROWSER_TEST_F(AerieSidebarMediaUiTest,
                       TheCardGoesWhenThePlaybackEnds) {
  ForceReducedMotion(true);
  content::WebContents* tab = OpenMediaTab();
  Play(tab);
  WaitForCard();
  ASSERT_TRUE(content::ExecJs(tab, "stopMedia()"));
  WaitForNoCard();
}

IN_PROC_BROWSER_TEST_F(AerieSidebarMediaUiTest, TheCardGoesWhenTheTabCloses) {
  ForceReducedMotion(true);
  content::WebContents* tab = OpenMediaTab();
  Play(tab);
  WaitForCard();
  TabStripModel* model = browser()->tab_strip_model();
  model->CloseWebContentsAt(model->GetIndexOfWebContents(tab),
                            TabCloseTypes::CLOSE_NONE);
  WaitForNoCard();
}

IN_PROC_BROWSER_TEST_F(AerieSidebarMediaUiTest,
                       TheCardFollowsTheTabThatStartedPlayingLast) {
  ForceReducedMotion(true);
  content::WebContents* first = OpenMediaTab();
  content::WebContents* second = OpenMediaTab();
  Play(first);
  WaitForCard();
  ASSERT_EQ(sidebar()->media()->shown_contents(), first);
  Play(second);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return sidebar()->media()->shown_contents() == second; }));
}

IN_PROC_BROWSER_TEST_F(AerieSidebarMediaUiTest,
                       ReducedMotionShowsTheCardWithoutAMove) {
  ForceReducedMotion(true);
  content::WebContents* tab = OpenMediaTab();
  bool saw_transform = false;
  bool saw_opacity = false;
  Play(tab);
  ASSERT_TRUE(base::test::RunUntil([&] {
    ui::Layer* layer = card()->motion_layer();
    if (layer) {
      saw_transform |= layer->GetAnimator()->IsAnimatingProperty(
          ui::LayerAnimationElement::TRANSFORM);
      saw_opacity |= layer->GetAnimator()->IsAnimatingProperty(
          ui::LayerAnimationElement::OPACITY);
    }
    return card()->shown() && (!layer || !layer->GetAnimator()->is_animating());
  }));
  EXPECT_FALSE(saw_transform);
  // The card fades in: opacity moves, and only that.
  EXPECT_TRUE(saw_opacity);
}

IN_PROC_BROWSER_TEST_F(AerieSidebarMediaUiTest, FullMotionRisesWhileItFadesIn) {
  ForceReducedMotion(false);
  content::WebContents* tab = OpenMediaTab();
  bool saw_transform = false;
  Play(tab);
  ASSERT_TRUE(base::test::RunUntil([&] {
    ui::Layer* layer = card()->motion_layer();
    if (layer) {
      saw_transform |= layer->GetAnimator()->IsAnimatingProperty(
          ui::LayerAnimationElement::TRANSFORM);
    }
    return card()->shown() && (!layer || !layer->GetAnimator()->is_animating());
  }));
  // The same steps with the switch off make the rise: the switch matters.
  EXPECT_TRUE(saw_transform);
}
