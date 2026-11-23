// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

// Interactive UI tests of the tab preview card (issue #270), with a real
// pointer: the card appears only after the standard hover delay and not if the
// pointer leaves first; it sits beside the row, never over it, and stays on the
// screen at its edges with the sidebar on either side; a row with keyboard focus
// shows it too, and its content is the row's accessible description; no
// snapshot is ever shown for an incognito tab; and the Settings switch turns the
// snapshot off and keeps the text. With AERIE_HOVER_SHOT_DIR set, the last test
// also saves the card over the window in light and dark (for the screenshot
// gallery). Run with ./aerie test-cpp --target aerie_interactive_ui_tests.

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/environment.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/run_loop.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/task/single_thread_task_runner.h"
#include "base/test/run_until.h"
#include "base/test/test_future.h"
#include "base/time/time.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/thumbnails/thumbnail_tab_helper.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_hover_card.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_hover_card_view.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_peek_surface.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_row_view.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_view.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/views/focus/focus_manager.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/interactive_test_utils.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/aerie_sidebar/aerie_sidebar_prefs.h"
#include "components/prefs/pref_service.h"
#include "components/tabs/public/tab_interface.h"
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
#include "ui/color/color_provider_key.h"
#include "ui/gfx/animation/animation_test_api.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/snapshot/snapshot.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"

namespace {

// A page whose title is the last part of its path: /page/Alpha is "Alpha".
std::unique_ptr<net::test_server::HttpResponse> ServePage(
    const net::test_server::HttpRequest& request) {
  constexpr char kPrefix[] = "/page/";
  if (request.relative_url.compare(0, sizeof(kPrefix) - 1, kPrefix) != 0) {
    return nullptr;
  }
  auto response = std::make_unique<net::test_server::BasicHttpResponse>();
  response->set_content_type("text/html");
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

std::u16string DescriptionOf(views::View* view) {
  ui::AXNodeData data;
  view->GetViewAccessibility().GetAccessibleNodeData(&data);
  return std::u16string(
      data.GetString16Attribute(ax::mojom::StringAttribute::kDescription));
}

gfx::ImageSkia SolidSnapshot() {
  SkBitmap bitmap;
  bitmap.allocN32Pixels(16, 10);
  bitmap.eraseARGB(255, 40, 120, 200);
  return gfx::ImageSkia::CreateFromBitmap(bitmap, 1.0f);
}

}  // namespace

class AerieSidebarHoverCardUiTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    // The card is on screen as soon as it is asked for, whatever its fade is
    // doing; the delay and the places are what is tested here.
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
  AerieSidebarHoverCard* hover() { return sidebar()->hover_card(); }
  TabStripModel* tabs() { return browser()->tab_strip_model(); }
  PrefService* prefs() { return browser()->GetProfile()->GetPrefs(); }

  void OpenTab(BrowserWindowInterface* in,
               const std::string& title) {
    ASSERT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
        in, embedded_test_server()->GetURL("localhost", "/page/" + title),
        WindowOpenDisposition::kNewForegroundTab,
        ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
  }
  // Alpha, Bravo and Charlie, and a row for each, grown to its height.
  void OpenThreeTabs() {
    ASSERT_TRUE(ui_test_utils::NavigateToURL(
        browser(), embedded_test_server()->GetURL("localhost", "/page/Alpha")));
    OpenTab(browser(), "Bravo");
    OpenTab(browser(), "Charlie");
    ASSERT_EQ(tabs()->count(), 3);
    WaitForRows(3u);
  }
  void WaitForRows(size_t count) {
    ASSERT_TRUE(base::test::RunUntil([&] {
      if (sidebar()->row_count() != count) {
        return false;
      }
      for (size_t i = 0; i < count; ++i) {
        const AerieSidebarRowView* row = sidebar()->row_at(i);
        if (row->height() < 20 || row->width() < 20) {
          return false;
        }
      }
      return true;
    }));
    // The rows settle.
    Pause(300);
  }

  AerieSidebarRowView* Row(size_t index) { return sidebar()->row_at(index); }
  void MoveTo(const gfx::Point& screen_point) {
    ASSERT_TRUE(ui_test_utils::SendMouseMoveSync(screen_point));
  }
  void Hover(views::View* view) {
    MoveTo(view->GetBoundsInScreen().CenterPoint());
  }
  // Over the page, away from every row.
  void MoveAway() {
    MoveTo(BrowserView::GetBrowserViewForBrowser(browser())
               ->GetActiveContentsWebView()
               ->GetBoundsInScreen()
               .CenterPoint());
  }
  void HoverAndWaitForCard(views::View* view) {
    Hover(view);
    ASSERT_TRUE(base::test::RunUntil([&] { return hover()->showing(); }));
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

IN_PROC_BROWSER_TEST_F(AerieSidebarHoverCardUiTest,
                       TheCardAppearsOnlyAfterTheHoverDelay) {
  OpenThreeTabs();
  ASSERT_FALSE(hover()->showing());
  const base::TimeTicks start = base::TimeTicks::Now();
  Hover(Row(0));
  // The pointer has only just arrived.
  EXPECT_FALSE(hover()->showing());
  EXPECT_TRUE(hover()->pending());
  ASSERT_TRUE(base::test::RunUntil([&] { return hover()->showing(); }));
  EXPECT_GE(base::TimeTicks::Now() - start, AerieSidebarHoverCard::ShowDelay());
  EXPECT_FALSE(hover()->pending());
  EXPECT_EQ(hover()->anchor(), Row(0));
}

IN_PROC_BROWSER_TEST_F(AerieSidebarHoverCardUiTest,
                       NoCardShowsIfThePointerLeavesBeforeTheDelay) {
  OpenThreeTabs();
  Hover(Row(1));
  ASSERT_TRUE(hover()->pending());
  MoveAway();
  EXPECT_FALSE(hover()->pending());
  // Wait past the delay: nothing comes.
  Pause(static_cast<int>(AerieSidebarHoverCard::ShowDelay().InMilliseconds()) +
        300);
  EXPECT_FALSE(hover()->showing());
}

IN_PROC_BROWSER_TEST_F(AerieSidebarHoverCardUiTest,
                       TheCardShowsTheTitleTheHostAndTheFullUrlOnItsOwnLine) {
  OpenThreeTabs();
  HoverAndWaitForCard(Row(0));
  AerieHoverCardBody* body = hover()->card()->body();
  const std::u16string title(body->title_label()->GetText());
  ASSERT_FALSE(title.empty());
  EXPECT_EQ(body->host_label()->GetText(), u"localhost");
  // The URL is the full address of the tab with this title.
  std::string url;
  for (int i = 0; i < tabs()->count(); ++i) {
    content::WebContents* contents = tabs()->GetWebContentsAt(i);
    if (contents->GetTitle() == title) {
      url = contents->GetLastCommittedURL().spec();
    }
  }
  ASSERT_FALSE(url.empty());
  EXPECT_EQ(base::UTF16ToUTF8(std::u16string(body->url_label()->GetText())), url);
  EXPECT_GT(body->url_label()->y(), body->host_label()->y());
}

IN_PROC_BROWSER_TEST_F(AerieSidebarHoverCardUiTest,
                       TheCardSitsBesideTheRowAndNeverCoversIt) {
  OpenThreeTabs();
  HoverAndWaitForCard(Row(1));
  const gfx::Rect row = Row(1)->GetBoundsInScreen();
  const gfx::Rect card = hover()->card_bounds_in_screen();
  EXPECT_FALSE(card.Intersects(row));
  // The sidebar is on the left: the card is on the page side of the row.
  EXPECT_GE(card.x(), row.right());
  EXPECT_EQ(card.y(), row.y());
}

IN_PROC_BROWSER_TEST_F(AerieSidebarHoverCardUiTest,
                       TheCardStaysOnScreenForTheTopAndTheBottomRows) {
  OpenThreeTabs();
  const gfx::Rect top = Row(0)->GetBoundsInScreen();
  const gfx::Rect bottom = Row(2)->GetBoundsInScreen();
  const gfx::Rect first = top.y() < bottom.y() ? top : bottom;
  const gfx::Rect last = top.y() < bottom.y() ? bottom : top;
  views::View* first_row = top.y() < bottom.y() ? Row(0) : Row(2);
  views::View* last_row = top.y() < bottom.y() ? Row(2) : Row(0);

  // A screen that ends at the bottom of the last row: the card, taller than the
  // row, moves up to stay in it.
  const gfx::Rect low_screen(first.x(), last.bottom() - 320, 1200, 320);
  hover()->SetScreenBoundsForTesting(low_screen);
  HoverAndWaitForCard(last_row);
  gfx::Rect card = hover()->card_bounds_in_screen();
  EXPECT_TRUE(low_screen.Contains(card)) << card.ToString();
  EXPECT_FALSE(card.Intersects(last));
  MoveAway();
  ASSERT_TRUE(base::test::RunUntil([&] { return !hover()->showing(); }));

  // A screen that starts below the top of the first row: the card moves down.
  const gfx::Rect high_screen(first.x(), first.y() + 10, 1200, 320);
  hover()->SetScreenBoundsForTesting(high_screen);
  HoverAndWaitForCard(first_row);
  card = hover()->card_bounds_in_screen();
  EXPECT_TRUE(high_screen.Contains(card)) << card.ToString();
  EXPECT_FALSE(card.Intersects(first));
}

IN_PROC_BROWSER_TEST_F(AerieSidebarHoverCardUiTest,
                       ASidebarOnTheRightGetsTheCardOnItsLeft) {
  OpenThreeTabs();
  prefs()->SetString(aerie_sidebar::kSidePref, "right");
  const int middle = BrowserView::GetBrowserViewForBrowser(browser())
                         ->GetBoundsInScreen()
                         .CenterPoint()
                         .x();
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return Row(1)->GetBoundsInScreen().x() > middle; }));
  Pause(300);
  HoverAndWaitForCard(Row(1));
  const gfx::Rect row = Row(1)->GetBoundsInScreen();
  const gfx::Rect card = hover()->card_bounds_in_screen();
  EXPECT_FALSE(card.Intersects(row));
  EXPECT_LE(card.right(), row.x());

  // And it stays on the screen at the right edge too: a screen that ends at the
  // right edge of the row leaves no room on the right.
  MoveAway();
  ASSERT_TRUE(base::test::RunUntil([&] { return !hover()->showing(); }));
  const gfx::Rect narrow(row.x() - 400, row.y() - 50, 400 + row.width(), 300);
  hover()->SetScreenBoundsForTesting(narrow);
  HoverAndWaitForCard(Row(1));
  EXPECT_TRUE(narrow.Contains(hover()->card_bounds_in_screen()));
  EXPECT_FALSE(hover()->card_bounds_in_screen().Intersects(row));
}

// The key that moves focus along the list from `row`: down, or up when `row` is
// the last one (the tab stop is the open tab's row, which may be the last).
ui::KeyboardCode AlongTheList(AerieSidebarView* sidebar, views::View* row) {
  for (size_t i = 0; i < sidebar->row_count(); ++i) {
    if (sidebar->row_at(i)->GetBoundsInScreen().y() >
        row->GetBoundsInScreen().y()) {
      return ui::VKEY_DOWN;
    }
  }
  return ui::VKEY_UP;
}

IN_PROC_BROWSER_TEST_F(AerieSidebarHoverCardUiTest,
                       ProgrammaticFocusOnARowShowsNoCard) {
  OpenThreeTabs();
  // Focus set by code (as focus restoration does) is not the user's attention.
  sidebar()->today_tree()->RequestFocus();
  ASSERT_TRUE(sidebar()->today_tree()->GetFocusManager()->GetFocusedView());
  EXPECT_FALSE(hover()->showing());
  EXPECT_FALSE(hover()->pending());
}

IN_PROC_BROWSER_TEST_F(AerieSidebarHoverCardUiTest,
                       KeyboardFocusOnARowShowsTheCardAndDescribesTheRow) {
  OpenThreeTabs();
  sidebar()->today_tree()->RequestFocus();
  views::View* first =
      sidebar()->today_tree()->GetFocusManager()->GetFocusedView();
  ASSERT_TRUE(first);
  ASSERT_FALSE(hover()->showing());

  // A real arrow key moves focus to the next row, with the card up at once.
  ASSERT_TRUE(ui_test_utils::SendKeyPressSync(
      browser(), AlongTheList(sidebar(), first), false, false, false, false));
  ASSERT_TRUE(base::test::RunUntil([&] { return hover()->showing(); }));
  views::View* second = hover()->anchor();
  ASSERT_TRUE(second);
  EXPECT_NE(second, first);
  EXPECT_TRUE(second->HasFocus());
  const std::u16string host(hover()->card()->body()->host_label()->GetText());
  const std::u16string url(hover()->card()->body()->url_label()->GetText());
  EXPECT_EQ(host, u"localhost");
  // The content of the card is the row's accessible description.
  EXPECT_EQ(DescriptionOf(second), host + u", " + url);
  EXPECT_TRUE(DescriptionOf(first).empty());

  // Focus leaves: the card and the description go.
  second->GetFocusManager()->ClearFocus();
  ASSERT_TRUE(base::test::RunUntil([&] { return !hover()->showing(); }));
  EXPECT_TRUE(DescriptionOf(second).empty());
}

IN_PROC_BROWSER_TEST_F(AerieSidebarHoverCardUiTest,
                       EscDismissesTheCardAndNothingElseWhileItIsUp) {
  OpenThreeTabs();
  sidebar()->today_tree()->RequestFocus();
  views::View* first =
      sidebar()->today_tree()->GetFocusManager()->GetFocusedView();
  ASSERT_TRUE(first);
  ASSERT_TRUE(ui_test_utils::SendKeyPressSync(
      browser(), AlongTheList(sidebar(), first), false, false, false, false));
  ASSERT_TRUE(base::test::RunUntil([&] { return hover()->showing(); }));
  views::View* row = hover()->anchor();
  ASSERT_TRUE(row);

  // Esc closes the card, and moves neither the focus nor the pointer.
  ASSERT_TRUE(ui_test_utils::SendKeyPressSync(
      browser(), ui::VKEY_ESCAPE, false, false, false, false));
  EXPECT_FALSE(hover()->showing());
  EXPECT_TRUE(DescriptionOf(row).empty());
  EXPECT_TRUE(row->HasFocus());

  // With no card up, Esc is as before: it does not bring one back.
  ASSERT_TRUE(ui_test_utils::SendKeyPressSync(
      browser(), ui::VKEY_ESCAPE, false, false, false, false));
  EXPECT_FALSE(hover()->showing());
  EXPECT_TRUE(row->HasFocus());
}

IN_PROC_BROWSER_TEST_F(AerieSidebarHoverCardUiTest,
                       APressOnAChildOfTheRowAlsoClosesTheCard) {
  OpenThreeTabs();
  HoverAndWaitForCard(Row(0));
  // The close button shows while the row is hovered. The pointer moves onto it
  // (still inside the row), and the card stays.
  views::View* close = Row(0)->close_button();
  ASSERT_TRUE(close->GetVisible());
  MoveTo(close->GetBoundsInScreen().CenterPoint());
  ASSERT_TRUE(hover()->showing());
  ASSERT_TRUE(ui_test_utils::SendMouseEventsSync(ui_controls::LEFT,
                                                 ui_controls::DOWN));
  EXPECT_FALSE(hover()->showing());
  ASSERT_TRUE(
      ui_test_utils::SendMouseEventsSync(ui_controls::LEFT, ui_controls::UP));
}

IN_PROC_BROWSER_TEST_F(AerieSidebarHoverCardUiTest,
                       ANewTitleReachesTheCardAndTheDescriptionWhileItIsOpen) {
  OpenThreeTabs();
  HoverAndWaitForCard(Row(0));
  const std::u16string title(hover()->card()->body()->title_label()->GetText());
  content::WebContents* contents = nullptr;
  for (int i = 0; i < tabs()->count(); ++i) {
    if (tabs()->GetWebContentsAt(i)->GetTitle() == title) {
      contents = tabs()->GetWebContentsAt(i);
    }
  }
  ASSERT_TRUE(contents);
  ASSERT_TRUE(content::ExecJs(contents, "document.title = 'Renamed'"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return hover()->showing() &&
           hover()->card()->body()->title_label()->GetText() == u"Renamed";
  }));
  // The card still describes the row: host and address.
  EXPECT_EQ(DescriptionOf(hover()->anchor()),
            u"localhost, " + std::u16string(hover()->card()->body()->url_label()->GetText()));
}

IN_PROC_BROWSER_TEST_F(AerieSidebarHoverCardUiTest, APressClosesTheCard) {
  OpenThreeTabs();
  HoverAndWaitForCard(Row(0));
  ASSERT_TRUE(ui_test_utils::SendMouseEventsSync(ui_controls::LEFT,
                                                 ui_controls::DOWN));
  EXPECT_FALSE(hover()->showing());
  ASSERT_TRUE(
      ui_test_utils::SendMouseEventsSync(ui_controls::LEFT, ui_controls::UP));
  // It stays away until the pointer comes back.
  Pause(static_cast<int>(AerieSidebarHoverCard::ShowDelay().InMilliseconds()) +
        300);
  EXPECT_FALSE(hover()->showing());
}

IN_PROC_BROWSER_TEST_F(AerieSidebarHoverCardUiTest,
                       ASnapshotShowsForATabThatIsNotPrivate) {
  OpenThreeTabs();
  hover()->SetSnapshotSourceForTesting(
      base::BindRepeating([](tabs::TabInterface*) { return SolidSnapshot(); }));
  EXPECT_TRUE(prefs()->GetBoolean(aerie_sidebar::kHoverCardSnapshotsPref));
  HoverAndWaitForCard(Row(0));
  EXPECT_TRUE(hover()->card()->body()->snapshot_shown());
}

IN_PROC_BROWSER_TEST_F(AerieSidebarHoverCardUiTest,
                       TheSettingsSwitchOffKeepsTheTextAndDropsTheSnapshot) {
  OpenThreeTabs();
  int asked = 0;
  hover()->SetSnapshotSourceForTesting(base::BindRepeating(
      [](int* asked, tabs::TabInterface*) {
        ++*asked;
        return SolidSnapshot();
      },
      &asked));
  // The switch in Settings writes this pref; on by default.
  EXPECT_TRUE(prefs()->GetBoolean(aerie_sidebar::kHoverCardSnapshotsPref));
  prefs()->SetBoolean(aerie_sidebar::kHoverCardSnapshotsPref, false);
  HoverAndWaitForCard(Row(0));
  EXPECT_FALSE(hover()->card()->body()->snapshot_shown());
  EXPECT_EQ(asked, 0);
  EXPECT_EQ(hover()->card()->body()->host_label()->GetText(), u"localhost");
  EXPECT_FALSE(hover()->card()->body()->url_label()->GetText().empty());

  // On again: the next card has its picture.
  MoveAway();
  ASSERT_TRUE(base::test::RunUntil([&] { return !hover()->showing(); }));
  prefs()->SetBoolean(aerie_sidebar::kHoverCardSnapshotsPref, true);
  HoverAndWaitForCard(Row(1));
  EXPECT_TRUE(hover()->card()->body()->snapshot_shown());
}

IN_PROC_BROWSER_TEST_F(AerieSidebarHoverCardUiTest,
                       NoSnapshotIsEverShownForAnIncognitoTab) {
  BrowserWindowInterface* incognito = CreateIncognitoBrowser();
  ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(incognito));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return SidebarOf(incognito) && SidebarOf(incognito)->hover_card(); }));
  OpenTab(incognito, "Bravo");
  AerieSidebarView* side = SidebarOf(incognito);
  ASSERT_TRUE(base::test::RunUntil([&] { return side->row_count() == 2u; }));
  // A source that would hand out a picture for any tab: it must not be asked
  // for a private one, and its picture must not reach the card.
  int asked = 0;
  side->hover_card()->SetSnapshotSourceForTesting(base::BindRepeating(
      [](int* asked, tabs::TabInterface*) {
        ++*asked;
        return SolidSnapshot();
      },
      &asked));
  // Focus by code shows no card; focus by Tab traversal (the focus manager's
  // reason for it) does. The real keys are tested in the window above.
  side->today_tree()->RequestFocus();
  views::View* focused = side->today_tree()->GetFocusManager()->GetFocusedView();
  ASSERT_TRUE(focused);
  focused->GetFocusManager()->ClearFocus();
  focused->GetFocusManager()->SetFocusedViewWithReason(
      focused, views::FocusManager::FocusChangeReason::kFocusTraversal);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return side->hover_card()->showing(); }));
  EXPECT_FALSE(side->hover_card()->card()->body()->snapshot_shown());
  EXPECT_EQ(asked, 0);
  // The text is there all the same.
  EXPECT_EQ(side->hover_card()->card()->body()->host_label()->GetText(),
            u"localhost");
}

// Saves the screenshots for the gallery when AERIE_HOVER_SHOT_DIR is set: the
// card over the window, with the real snapshot of a page, in light and dark.
IN_PROC_BROWSER_TEST_F(AerieSidebarHoverCardUiTest, CardScreenshots) {
  std::optional<std::string> dir =
      base::Environment::Create()->GetVar("AERIE_HOVER_SHOT_DIR");
  if (!dir || dir->empty()) {
    return;
  }
  OpenThreeTabs();
  views::Widget* browser_widget =
      BrowserView::GetBrowserViewForBrowser(browser())->GetWidget();
  // Chromium takes the picture of a tab when it is put in the background, but
  // only while something watches its thumbnail. So each tab is watched, every
  // tab is brought to the front in turn, and the first one goes last.
  std::vector<std::unique_ptr<ThumbnailImage::Subscription>> watchers;
  for (int i = 0; i < tabs()->count(); ++i) {
    ThumbnailTabHelper* helper =
        ThumbnailTabHelper::From(tabs()->GetTabAtIndex(i));
    ASSERT_TRUE(helper && helper->thumbnail());
    watchers.push_back(helper->thumbnail()->Subscribe());
  }
  for (int i = 0; i < tabs()->count(); ++i) {
    tabs()->ActivateTabAt((tabs()->active_index() + 1) % tabs()->count());
    Pause(700);
  }
  tabs()->ActivateTabAt(tabs()->count() - 1);
  Pause(700);
  for (int i = 0; i < tabs()->count() - 1; ++i) {
    tabs::TabInterface* tab = tabs()->GetTabAtIndex(i);
    ASSERT_TRUE(base::test::RunUntil([&] {
      return ThumbnailTabHelper::From(tab)->thumbnail()->has_data();
    })) << "no thumbnail for tab " << i;
  }
  // A row of a tab that is not in front, with a thumbnail.
  AerieSidebarRowView* row = nullptr;
  for (size_t i = 0; i < sidebar()->row_count(); ++i) {
    if (!sidebar()->row_at(i)->selected()) {
      row = sidebar()->row_at(i);
      break;
    }
  }
  ASSERT_TRUE(row);
  for (const bool dark : {false, true}) {
    browser_widget->SetColorModeOverride(
        dark ? ui::ColorProviderKey::ColorMode::kDark
             : ui::ColorProviderKey::ColorMode::kLight);
    Pause(500);
    Hover(row);
    ASSERT_TRUE(base::test::RunUntil([&] { return hover()->showing(); }));
    ASSERT_TRUE(base::test::RunUntil(
        [&] { return hover()->card()->body()->snapshot_shown(); }));
    Pause(500);
    const gfx::Point origin =
        hover()->card()->GetWidget()->GetWindowBoundsInScreen().origin() -
        browser_widget->GetNativeWindow()->GetBoundsInScreen().origin().OffsetFromOrigin();
    ASSERT_TRUE(base::WriteFile(
        base::FilePath::FromUTF8Unsafe(*dir).AppendASCII(
            dark ? "hover-card-dark.pos" : "hover-card-light.pos"),
        base::StringPrintf("%d %d %d", origin.x(), origin.y(),
                           AerieSidebarPeekSurface::ShadowRoom())));
    for (views::Widget* widget :
         {browser_widget, hover()->card()->GetWidget()}) {
      gfx::NativeWindow window = widget->GetNativeWindow();
      base::test::TestFuture<scoped_refptr<base::RefCountedMemory>> png;
      ui::GrabWindowSnapshotAsPNG(
          window, gfx::Rect(window->bounds().size()), png.GetCallback());
      ASSERT_TRUE(png.Get() && png.Get()->size() > 0);
      ASSERT_TRUE(base::WriteFile(
          base::FilePath::FromUTF8Unsafe(*dir).AppendASCII(
              std::string("hover-card") +
              (widget == browser_widget ? "-window" : "") +
              (dark ? "-dark.png" : "-light.png")),
          *png.Get()));
    }
    MoveAway();
    ASSERT_TRUE(base::test::RunUntil([&] { return !hover()->showing(); }));
  }
}
