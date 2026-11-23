// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

// Interactive UI tests of Ctrl+Tab and the recent tabs overlay (issue #269),
// with real key events: holding Ctrl and pressing Tab shows a card for each
// recent tab with the previous one selected; Tab again steps along, and
// Ctrl+Shift+Tab steps back; releasing Ctrl switches to the selected tab; Esc
// cancels. The Settings switch "Ctrl+Tab cycles recent tabs" (the pref it writes)
// turns it off and Ctrl+Tab goes along the tab order again. A snapshot shows on
// a card only for a tab that is not private. With AERIE_RECENT_SHOT_DIR set, the
// last test also saves the overlay in light and dark (for the screenshot
// gallery). Run with ./aerie test-cpp --target aerie_interactive_ui_tests.

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/environment.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/run_loop.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "base/test/run_until.h"
#include "base/test/test_future.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/thumbnails/thumbnail_tab_helper.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_peek_surface.h"
#include "components/tabs/public/tab_interface.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_recent_tabs.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_recent_tabs_overlay.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_view.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/interactive_test_utils.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/aerie_sidebar/aerie_sidebar_prefs.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/aura/window.h"
#include "ui/base/test/ui_controls.h"
#include "ui/color/color_provider_key.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/animation/animation_test_api.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/snapshot/snapshot.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"

namespace {

// A page whose title is the last part of its path: /page/Alpha is "Alpha".
std::unique_ptr<net::test_server::HttpResponse> ServePage(
    const net::test_server::HttpRequest& request) {
  constexpr char kPrefix[] = "/page/";
  if (request.relative_url == "/keys") {
    // A page that records the keys it is sent.
    auto response = std::make_unique<net::test_server::BasicHttpResponse>();
    response->set_content_type("text/html");
    response->set_content(
        "<!doctype html><title>Keys</title><script>window.keys = [];"
        "addEventListener('keydown', e => window.keys.push(e.key));</script>"
        "<body>keys");
    return response;
  }
  if (!base::StartsWith(request.relative_url, kPrefix)) {
    return nullptr;
  }
  auto response = std::make_unique<net::test_server::BasicHttpResponse>();
  response->set_content_type("text/html");
  // A tint and a big heading of its own for each title, so that a thumbnail
  // of the page tells the tabs apart.
  const std::string title = request.relative_url.substr(sizeof(kPrefix) - 1);
  int hue = 0;
  for (char c : title) {
    hue = (hue * 37 + c) % 360;
  }
  response->set_content(
      "<!doctype html><title>" + title + "</title><body style=\"margin:0;"
      "background:hsl(" + base::NumberToString(hue) + ",55%,72%);"
      "font:700 64px sans-serif;padding:48px\">" + title);
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

AerieSidebarView* SidebarOf(BrowserWindowInterface* browser) {
  return FindDescendant<AerieSidebarView>(
      BrowserView::GetBrowserViewForBrowser(browser)
          ->vertical_tab_strip_region_view_for_testing());
}

std::u16string NameOf(views::View* view) {
  ui::AXNodeData data;
  view->GetViewAccessibility().GetAccessibleNodeData(&data);
  return std::u16string(
      data.GetString16Attribute(ax::mojom::StringAttribute::kName));
}

gfx::ImageSkia SolidSnapshot() {
  SkBitmap bitmap;
  bitmap.allocN32Pixels(16, 10);
  bitmap.eraseARGB(255, 40, 120, 200);
  return gfx::ImageSkia::CreateFromBitmap(bitmap, 1.0f);
}

}  // namespace

class AerieSidebarRecentTabsUiTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    // The overlay is on screen as soon as it is asked for, whatever its fade
    // is doing; the keys are what is tested here.
    reduced_motion_.emplace(
        gfx::AnimationTestApi::SetPrefersReducedMotionForTesting(true));
    ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));
    host_resolver()->AddRule("*", "127.0.0.1");
    embedded_test_server()->RegisterRequestHandler(
        base::BindRepeating(&ServePage));
    ASSERT_TRUE(embedded_test_server()->Start());
    ASSERT_TRUE(base::test::RunUntil(
        [&] { return sidebar() && sidebar()->row_count() == 1u; }));
  }
  void TearDownOnMainThread() override {
    reduced_motion_.reset();
    InProcessBrowserTest::TearDownOnMainThread();
  }

  AerieSidebarView* sidebar() { return SidebarOf(browser()); }
  AerieSidebarRecentTabs* recent() { return sidebar()->recent_tabs(); }
  TabStripModel* tabs() { return browser()->tab_strip_model(); }

  // Opens `title` on `host` in a new tab that comes to the front.
  void OpenTab(BrowserWindowInterface* in, const std::string& host, const std::string& title) {
    ASSERT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
        in, embedded_test_server()->GetURL(host, "/page/" + title),
        WindowOpenDisposition::kNewForegroundTab,
        ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
  }
  // The pages are on localhost: Aerie's HTTPS-only default lets it through and
  // not other plain http hosts. The sites differ in the path only.
  // The first tab is Alpha, then Bravo, Charlie and Delta open in that order.
  // Each was in front when the next one opened, so the recent order is Delta,
  // Charlie, Bravo, Alpha, and the tab order is Alpha, Bravo, Charlie, Delta.
  void OpenFourTabs() {
    ASSERT_TRUE(ui_test_utils::NavigateToURL(
        browser(), embedded_test_server()->GetURL("localhost", "/page/Alpha")));
    OpenTab(browser(), "localhost", "Bravo");
    OpenTab(browser(), "localhost", "Charlie");
    OpenTab(browser(), "localhost", "Delta");
    ASSERT_EQ(tabs()->count(), 4);
  }

  std::u16string ActiveTitle() {
    return tabs()->GetActiveWebContents()->GetTitle();
  }
  void ExpectActive(const std::u16string& title) {
    ASSERT_TRUE(base::test::RunUntil([&] { return ActiveTitle() == title; }));
  }

  // Sends key events to the window, and waits until they have been handled.
  void SendKeys(ui::KeyboardCode key, int event_types, int accelerators) {
    base::RunLoop loop;
    ASSERT_TRUE(ui_controls::SendKeyEventsNotifyWhenDone(
        browser()->GetWindow()->GetNativeWindow(), key, event_types,
        loop.QuitClosure(), accelerators));
    loop.Run();
  }
  // Ctrl goes down and Tab is pressed, and nothing is let go: the user holds
  // Ctrl until ReleaseCtrl().
  void HoldCtrlAndPressTab(bool shift = false) {
    SendKeys(ui::VKEY_TAB, ui_controls::kKeyPress,
             ui_controls::kControl | (shift ? ui_controls::kShift : 0));
  }
  void ReleaseCtrl() {
    SendKeys(ui::VKEY_CONTROL,
             ui_controls::kKeyPress | ui_controls::kKeyRelease,
             ui_controls::kNoAccelerator);
  }
  void PressEscape() {
    SendKeys(ui::VKEY_ESCAPE, ui_controls::kKeyPress | ui_controls::kKeyRelease,
             ui_controls::kNoAccelerator);
  }

  std::u16string SelectedTitle() {
    return std::u16string(
        recent()
            ->overlay()
            ->card_at(recent()->overlay()->selected_index())
            ->title_label()
            ->GetText());
  }
  void WaitForOverlay() {
    ASSERT_TRUE(base::test::RunUntil([&] { return recent()->showing(); }));
  }
  void WaitForNoOverlay() {
    ASSERT_TRUE(base::test::RunUntil([&] { return !recent()->showing(); }));
  }

  // With AERIE_RECENT_SHOT_DIR set, saves screenshots of the overlay (and of the
  // window under it) as <name>-light.png and <name>-dark.png (for the
  // screenshot gallery). The overlay takes its colours when it opens, so it is
  // opened again in each colour mode.
  void Shoot(const std::string& name) {
    std::optional<std::string> dir =
        base::Environment::Create()->GetVar("AERIE_RECENT_SHOT_DIR");
    if (!dir || dir->empty()) {
      return;
    }
    views::Widget* browser_widget =
        BrowserView::GetBrowserViewForBrowser(browser())->GetWidget();
    // Chromium takes the picture of a tab when it is put in the background, but
    // only while something watches its thumbnail (a hover card does). So each
    // tab is watched, every tab is brought to the front in turn, and the tab
    // that was in front at the start goes last.
    std::vector<std::unique_ptr<ThumbnailImage::Subscription>> watchers;
    for (int i = 0; i < tabs()->count(); ++i) {
      ThumbnailTabHelper* helper =
          ThumbnailTabHelper::From(tabs()->GetTabAtIndex(i));
      ASSERT_TRUE(helper && helper->thumbnail());
      watchers.push_back(helper->thumbnail()->Subscribe());
    }
    const int last = tabs()->active_index();
    for (int i = 0; i < tabs()->count(); ++i) {
      const int index = (last + 1 + i) % tabs()->count();
      tabs()->ActivateTabAt(index);
      Pause(700);
    }
    for (int i = 0; i < tabs()->count(); ++i) {
      if (i == tabs()->active_index()) {
        continue;
      }
      tabs::TabInterface* tab = tabs()->GetTabAtIndex(i);
      ASSERT_TRUE(base::test::RunUntil([&] {
        return ThumbnailTabHelper::From(tab)->thumbnail()->has_data();
      })) << "no thumbnail for tab " << i;
    }
    for (const bool dark : {false, true}) {
      browser_widget->SetColorModeOverride(
          dark ? ui::ColorProviderKey::ColorMode::kDark
               : ui::ColorProviderKey::ColorMode::kLight);
      Pause(500);  // The sidebar repaints with the new palette.
      HoldCtrlAndPressTab();
      HoldCtrlAndPressTab();
      WaitForOverlay();
      Pause(500);
      {
        // Where the overlay lies in the window's picture, for the composite:
        // "x y shadow_room".
        const gfx::Point origin =
            recent()->overlay()->GetWidget()->GetWindowBoundsInScreen().origin() -
            browser_widget->GetNativeWindow()->GetBoundsInScreen().origin().OffsetFromOrigin();
        ASSERT_TRUE(base::WriteFile(
            base::FilePath::FromUTF8Unsafe(*dir).AppendASCII(
                name + (dark ? "-dark.pos" : "-light.pos")),
            base::StringPrintf("%d %d %d", origin.x(), origin.y(),
                               AerieSidebarPeekSurface::ShadowRoom())));
      }
      for (views::Widget* widget :
           {browser_widget, recent()->overlay()->GetWidget()}) {
        gfx::NativeWindow window = widget->GetNativeWindow();
        base::test::TestFuture<scoped_refptr<base::RefCountedMemory>> png;
        ui::GrabWindowSnapshotAsPNG(
            window, gfx::Rect(window->bounds().size()), png.GetCallback());
        ASSERT_TRUE(png.Get() && png.Get()->size() > 0);
        ASSERT_TRUE(base::WriteFile(
            base::FilePath::FromUTF8Unsafe(*dir).AppendASCII(
                name + (widget == browser_widget ? "-window" : "") +
                (dark ? "-dark.png" : "-light.png")),
            *png.Get()));
      }
      PressEscape();
      WaitForNoOverlay();
      ReleaseCtrl();
    }
  }
  void Pause(int ms) {
    base::RunLoop loop;
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, loop.QuitClosure(), base::Milliseconds(ms));
    loop.Run();
  }

 private:
  std::optional<gfx::AnimationTestApi::PrefersReducedMotionResetter>
      reduced_motion_;
};

IN_PROC_BROWSER_TEST_F(AerieSidebarRecentTabsUiTest,
                       HoldingCtrlAndPressingTabShowsTheCardsWithThePreviousTabSelected) {
  OpenFourTabs();
  EXPECT_FALSE(recent()->showing());

  HoldCtrlAndPressTab();
  WaitForOverlay();

  // One card for each tab, most recent first, and the one before the active
  // tab is selected. The page is still Delta: nothing switches before Ctrl is
  // let go.
  AerieSidebarRecentTabsOverlay* overlay = recent()->overlay();
  ASSERT_EQ(overlay->card_count(), 4u);
  EXPECT_EQ(overlay->card_at(0)->title_label()->GetText(), u"Delta");
  EXPECT_EQ(overlay->card_at(1)->title_label()->GetText(), u"Charlie");
  EXPECT_EQ(overlay->card_at(2)->title_label()->GetText(), u"Bravo");
  EXPECT_EQ(overlay->card_at(3)->title_label()->GetText(), u"Alpha");
  EXPECT_EQ(overlay->selected_index(), 1u);
  EXPECT_TRUE(overlay->card_at(1)->selected());
  EXPECT_FALSE(overlay->card_at(0)->selected());
  EXPECT_EQ(ActiveTitle(), u"Delta");

  ReleaseCtrl();
  WaitForNoOverlay();
}

IN_PROC_BROWSER_TEST_F(AerieSidebarRecentTabsUiTest,
                       TabAgainStepsAlongTheRecentOrderAndWraps) {
  OpenFourTabs();
  HoldCtrlAndPressTab();
  WaitForOverlay();
  EXPECT_EQ(SelectedTitle(), u"Charlie");

  HoldCtrlAndPressTab();
  EXPECT_EQ(SelectedTitle(), u"Bravo");
  HoldCtrlAndPressTab();
  EXPECT_EQ(SelectedTitle(), u"Alpha");
  // Past the last card it wraps to the first, which is the tab on screen.
  HoldCtrlAndPressTab();
  EXPECT_EQ(SelectedTitle(), u"Delta");
  HoldCtrlAndPressTab();
  EXPECT_EQ(SelectedTitle(), u"Charlie");

  ReleaseCtrl();
  WaitForNoOverlay();
  ExpectActive(u"Charlie");
}

IN_PROC_BROWSER_TEST_F(AerieSidebarRecentTabsUiTest,
                       CtrlShiftTabStepsBack) {
  OpenFourTabs();
  HoldCtrlAndPressTab();
  HoldCtrlAndPressTab();
  WaitForOverlay();
  EXPECT_EQ(SelectedTitle(), u"Bravo");

  HoldCtrlAndPressTab(/*shift=*/true);
  EXPECT_EQ(SelectedTitle(), u"Charlie");
  HoldCtrlAndPressTab(/*shift=*/true);
  EXPECT_EQ(SelectedTitle(), u"Delta");
  HoldCtrlAndPressTab(/*shift=*/true);
  EXPECT_EQ(SelectedTitle(), u"Alpha");

  ReleaseCtrl();
  WaitForNoOverlay();
  ExpectActive(u"Alpha");
}

IN_PROC_BROWSER_TEST_F(AerieSidebarRecentTabsUiTest,
                       ReleasingCtrlSwitchesToTheSelectedTab) {
  OpenFourTabs();
  HoldCtrlAndPressTab();
  WaitForOverlay();
  ReleaseCtrl();
  WaitForNoOverlay();
  ExpectActive(u"Charlie");

  // Charlie is now the most recent, and Delta the one before it: Ctrl+Tab goes
  // back to Delta. From Delta, the tab order would go on to Alpha (the strip
  // wraps), but the recent order goes back to Charlie.
  HoldCtrlAndPressTab();
  WaitForOverlay();
  EXPECT_EQ(SelectedTitle(), u"Delta");
  ReleaseCtrl();
  WaitForNoOverlay();
  ExpectActive(u"Delta");
  HoldCtrlAndPressTab();
  WaitForOverlay();
  EXPECT_EQ(SelectedTitle(), u"Charlie");
  ReleaseCtrl();
  WaitForNoOverlay();
  ExpectActive(u"Charlie");
}

IN_PROC_BROWSER_TEST_F(AerieSidebarRecentTabsUiTest,
                       EscapeCancelsAndStaysOnTheCurrentTab) {
  OpenFourTabs();
  HoldCtrlAndPressTab();
  HoldCtrlAndPressTab();
  WaitForOverlay();
  EXPECT_EQ(SelectedTitle(), u"Bravo");

  PressEscape();
  WaitForNoOverlay();
  ReleaseCtrl();
  // Letting go of Ctrl after the cancel switches nothing.
  EXPECT_EQ(ActiveTitle(), u"Delta");
  EXPECT_FALSE(recent()->showing());
}

IN_PROC_BROWSER_TEST_F(AerieSidebarRecentTabsUiTest,
                       EscapeClosesTheOverlayAndTheTabNeverSeesIt) {
  OpenFourTabs();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("localhost", "/keys")));
  content::WebContents* page = tabs()->GetActiveWebContents();
  page->Focus();
  // Control: with no overlay open, the page does get Escape.
  PressEscape();
  EXPECT_EQ(true, content::EvalJs(page, "window.keys.includes('Escape')"));
  ASSERT_TRUE(content::ExecJs(page, "window.keys = []"));

  HoldCtrlAndPressTab();
  WaitForOverlay();
  PressEscape();
  WaitForNoOverlay();
  ReleaseCtrl();
  // The keys the page was sent are in its list by now: this call is queued
  // behind them.
  EXPECT_EQ(false, content::EvalJs(page, "window.keys.includes('Escape')"));
  EXPECT_EQ(page, tabs()->GetActiveWebContents());
}

IN_PROC_BROWSER_TEST_F(AerieSidebarRecentTabsUiTest,
                       EveryTabCanBeReachedWhenThereAreMoreThanTheOverlayShows) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("localhost", "/page/T1")));
  for (int i = 2; i <= 8; ++i) {
    OpenTab(browser(), "localhost", "T" + base::NumberToString(i));
  }
  ASSERT_EQ(tabs()->count(), 8);
  // The recent order is T8 (on screen), T7, T6 ... T1.
  HoldCtrlAndPressTab();
  WaitForOverlay();
  EXPECT_EQ(SelectedTitle(), u"T7");
  const size_t shown = recent()->overlay()->card_count();
  EXPECT_LT(shown, 8u);  // the overlay has a cap
  for (int i = 0; i < 5; ++i) {
    HoldCtrlAndPressTab();
  }
  // The seventh most recent tab: the selection is on a card that is shown.
  EXPECT_EQ(SelectedTitle(), u"T2");
  EXPECT_LT(recent()->overlay()->selected_index(), recent()->overlay()->card_count());
  EXPECT_EQ(recent()->overlay()->card_count(), shown);
  ReleaseCtrl();
  WaitForNoOverlay();
  ExpectActive(u"T2");
}

IN_PROC_BROWSER_TEST_F(AerieSidebarRecentTabsUiTest,
                       AWindowWithOneTabShowsNoOverlay) {
  ASSERT_EQ(tabs()->count(), 1);
  HoldCtrlAndPressTab();
  EXPECT_FALSE(recent()->showing());
  ReleaseCtrl();
}

IN_PROC_BROWSER_TEST_F(AerieSidebarRecentTabsUiTest,
                       ACardShowsTheFaviconTitleAndSiteOfItsTab) {
  OpenFourTabs();
  HoldCtrlAndPressTab();
  WaitForOverlay();
  AerieRecentTabCard* card = recent()->overlay()->card_at(1);
  EXPECT_EQ(card->title_label()->GetText(), u"Charlie");
  EXPECT_EQ(card->site_label()->GetText(), u"localhost");
  // A page with no icon still draws one (the globe).
  EXPECT_TRUE(card->favicon_shown());
  // No snapshot has been taken of a tab that was just opened in a test.
  EXPECT_FALSE(card->snapshot_shown());
  ReleaseCtrl();
}

IN_PROC_BROWSER_TEST_F(AerieSidebarRecentTabsUiTest,
                       ASnapshotShowsOnTheCardOfATabThatIsNotPrivate) {
  OpenFourTabs();
  recent()->SetSnapshotSourceForTesting(
      base::BindRepeating([](tabs::TabInterface*) { return SolidSnapshot(); }));
  HoldCtrlAndPressTab();
  WaitForOverlay();
  for (size_t i = 0; i < recent()->overlay()->card_count(); ++i) {
    EXPECT_TRUE(recent()->overlay()->card_at(i)->snapshot_shown()) << i;
  }
  ReleaseCtrl();
}

IN_PROC_BROWSER_TEST_F(AerieSidebarRecentTabsUiTest,
                       NoSnapshotIsEverShownForAnIncognitoTab) {
  BrowserWindowInterface* incognito = CreateIncognitoBrowser();
  ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(incognito));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return SidebarOf(incognito) && SidebarOf(incognito)->recent_tabs(); }));
  OpenTab(incognito, "localhost", "Bravo");
  ASSERT_EQ(incognito->GetTabStripModel()->count(), 2);
  AerieSidebarRecentTabs* recent_tabs = SidebarOf(incognito)->recent_tabs();
  // A source that would hand out a picture for any tab: it must not be asked
  // for a private one, and its picture must not reach a card.
  int asked = 0;
  recent_tabs->SetSnapshotSourceForTesting(base::BindRepeating(
      [](int* asked, tabs::TabInterface*) {
        ++*asked;
        return SolidSnapshot();
      },
      &asked));
  base::RunLoop loop;
  ASSERT_TRUE(ui_controls::SendKeyEventsNotifyWhenDone(
      incognito->GetWindow()->GetNativeWindow(), ui::VKEY_TAB,
      ui_controls::kKeyPress, loop.QuitClosure(), ui_controls::kControl));
  loop.Run();
  ASSERT_TRUE(base::test::RunUntil([&] { return recent_tabs->showing(); }));
  ASSERT_EQ(recent_tabs->overlay()->card_count(), 2u);
  for (size_t i = 0; i < 2; ++i) {
    EXPECT_FALSE(recent_tabs->overlay()->card_at(i)->snapshot_shown()) << i;
  }
  EXPECT_EQ(asked, 0);
  base::RunLoop release;
  ASSERT_TRUE(ui_controls::SendKeyEventsNotifyWhenDone(
      incognito->GetWindow()->GetNativeWindow(), ui::VKEY_CONTROL,
      ui_controls::kKeyPress | ui_controls::kKeyRelease, release.QuitClosure(),
      ui_controls::kNoAccelerator));
  release.Run();
}

IN_PROC_BROWSER_TEST_F(AerieSidebarRecentTabsUiTest,
                       TheSettingsSwitchOffGivesTheTabOrderBack) {
  OpenFourTabs();
  PrefService* prefs = browser()->GetProfile()->GetPrefs();
  // On by default.
  EXPECT_TRUE(prefs->GetBoolean(aerie_sidebar::kRecentTabSwitcherPref));

  // The switch in Settings writes this pref.
  prefs->SetBoolean(aerie_sidebar::kRecentTabSwitcherPref, false);
  tabs()->ActivateTabAt(1);
  ExpectActive(u"Bravo");
  // Ctrl+Tab with the switch off goes to the next tab in the strip, with no
  // cards.
  ASSERT_TRUE(ui_test_utils::SendKeyPressSync(browser(), ui::VKEY_TAB, true,
                                              false, false, false));
  EXPECT_FALSE(recent()->showing());
  ExpectActive(u"Charlie");
  ASSERT_TRUE(ui_test_utils::SendKeyPressSync(browser(), ui::VKEY_TAB, true,
                                              false, false, false));
  ExpectActive(u"Delta");

  // On again: the cards come back.
  prefs->SetBoolean(aerie_sidebar::kRecentTabSwitcherPref, true);
  HoldCtrlAndPressTab();
  WaitForOverlay();
  ReleaseCtrl();
  WaitForNoOverlay();
  ExpectActive(u"Charlie");
}

IN_PROC_BROWSER_TEST_F(AerieSidebarRecentTabsUiTest,
                       TheSelectedCardIsAnnouncedAndNamedForAScreenReader) {
  OpenFourTabs();
  HoldCtrlAndPressTab();
  WaitForOverlay();
  AerieSidebarRecentTabsOverlay* overlay = recent()->overlay();
  EXPECT_EQ(NameOf(overlay), u"Recent tabs");
  ui::AXNodeData data;
  overlay->GetViewAccessibility().GetAccessibleNodeData(&data);
  EXPECT_EQ(data.role, ax::mojom::Role::kListBox);

  EXPECT_EQ(recent()->last_announcement(), u"Charlie, localhost, 2 of 4");
  EXPECT_EQ(NameOf(overlay->card_at(1)), u"Charlie, localhost");
  ui::AXNodeData card;
  overlay->card_at(1)->GetViewAccessibility().GetAccessibleNodeData(&card);
  EXPECT_EQ(card.role, ax::mojom::Role::kListBoxOption);
  EXPECT_TRUE(card.GetBoolAttribute(ax::mojom::BoolAttribute::kSelected));
  ui::AXNodeData other;
  overlay->card_at(0)->GetViewAccessibility().GetAccessibleNodeData(&other);
  EXPECT_FALSE(other.GetBoolAttribute(ax::mojom::BoolAttribute::kSelected));

  HoldCtrlAndPressTab();
  EXPECT_EQ(recent()->last_announcement(), u"Bravo, localhost, 3 of 4");
  ReleaseCtrl();
}

// Saves the screenshots for the gallery when AERIE_RECENT_SHOT_DIR is set.
IN_PROC_BROWSER_TEST_F(AerieSidebarRecentTabsUiTest, OverlayScreenshots) {
  OpenFourTabs();
  // The pictures on the cards are Chromium's own thumbnails of the pages.
  Shoot("recent-tabs");
}
