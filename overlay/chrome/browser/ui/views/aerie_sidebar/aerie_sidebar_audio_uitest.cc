// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

// Interactive UI tests of the audio mark in the sidebar (issue #268): a tab
// that plays sound shows a speaker in its row, a muted tab a crossed speaker,
// pressing the mark mutes or unmutes the tab without selecting it, and the mark
// is a real button that the keyboard reaches. With AERIE_MUTE_SHOT_DIR set, the
// last test also saves the rows in light and dark (for the screenshot gallery).
// Run with ./aerie test-cpp --target aerie_interactive_ui_tests.

#include <memory>
#include <optional>
#include <string>

#include "base/command_line.h"
#include "base/environment.h"
#include "base/files/file_util.h"
#include "base/run_loop.h"
#include "base/task/single_thread_task_runner.h"
#include "base/test/run_until.h"
#include "base/test/test_future.h"
#include "chrome/browser/aerie_picture_in_picture/aerie_auto_pip_fixture.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/alert/tab_alert_controller.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/tabs/public/tab_alert.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_audio_state.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_row_view.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_view.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/interactive_test_utils.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "media/base/media_switches.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/aura/window.h"
#include "ui/color/color_provider_key.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/snapshot/snapshot.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"

namespace {

// A page that plays sound: the WebM of the picture-in-picture test, inline as a
// data URL and looped. Nothing plays until the test calls audio.play().
constexpr char kPageTemplate[] = R"(<!doctype html>
<title>Fixture Song</title>
<body>
<video id=audio loop width=64 height=48 src="data:video/webm;base64,%BASE64%"></video>
)";

std::unique_ptr<net::test_server::HttpResponse> ServePage(
    const net::test_server::HttpRequest& request) {
  if (request.relative_url != "/sound.html") {
    return nullptr;
  }
  std::string page = kPageTemplate;
  page.replace(page.find("%BASE64%"), 8, kAerieAutoPipWebmBase64);
  auto response = std::make_unique<net::test_server::BasicHttpResponse>();
  response->set_content_type("text/html");
  response->set_content(page);
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

class AerieSidebarAudioUiTest : public InProcessBrowserTest {
 protected:
  using AudioState = AerieSidebarAudioState;

  void SetUpCommandLine(base::CommandLine* command_line) override {
    InProcessBrowserTest::SetUpCommandLine(command_line);
    // A test machine may have no sound card; the null sink plays to nowhere.
    command_line->AppendSwitch(switches::kDisableAudioOutput);
  }
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));
    host_resolver()->AddRule("*", "127.0.0.1");
    embedded_test_server()->RegisterRequestHandler(base::BindRepeating(&ServePage));
    ASSERT_TRUE(embedded_test_server()->Start());
    ASSERT_TRUE(base::test::RunUntil(
        [&] { return sidebar() && sidebar()->row_count() == 1u; }));
  }

  BrowserView* browser_view() {
    return BrowserView::GetBrowserViewForBrowser(browser());
  }
  AerieSidebarView* sidebar() {
    return FindDescendant<AerieSidebarView>(
        browser_view()->vertical_tab_strip_region_view_for_testing());
  }
  TabStripModel* strip() { return browser()->tab_strip_model(); }
  views::View* focused() {
    return browser_view()->GetWidget()->GetFocusManager()->GetFocusedView();
  }

  // Opens the sound page in a new tab, plays it, and then brings the first tab
  // to the front again (a tab that is never shown does not load its media on a
  // test machine). Returns the row of the sound tab, which is the second one.
  AerieSidebarRowView* OpenSoundTab() {
    EXPECT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
        browser(), embedded_test_server()->GetURL("localhost", "/sound.html"),
        WindowOpenDisposition::kNewForegroundTab,
        ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
    contents_ = strip()->GetWebContentsAt(strip()->count() - 1);
    EXPECT_TRUE(content::ExecJs(contents_, "audio.play(); 0",
                                content::EXECUTE_SCRIPT_NO_RESOLVE_PROMISES));
    EXPECT_TRUE(base::test::RunUntil([&] {
      return sidebar()->row_at(1)->audio_state() == AudioState::kPlaying;
    }));
    strip()->ActivateTabAt(0);
    EXPECT_TRUE(base::test::RunUntil([&] { return strip()->active_index() == 0; }));
    return sidebar()->row_at(1);
  }

  // A real click on `view`: the press and release go through the widget, so the
  // view under the pointer gets them (the button, not the row behind it).
  void Click(views::View* view) {
    gfx::Point root(view->width() / 2, view->height() / 2);
    views::View::ConvertPointToWidget(view, &root);
    views::Widget* widget = view->GetWidget();
    ui::MouseEvent press(ui::EventType::kMousePressed, root, root,
                         base::TimeTicks::Now(), ui::EF_LEFT_MOUSE_BUTTON,
                         ui::EF_LEFT_MOUSE_BUTTON);
    widget->OnMouseEvent(&press);
    ui::MouseEvent release(ui::EventType::kMouseReleased, root, root,
                           base::TimeTicks::Now(), ui::EF_LEFT_MOUSE_BUTTON,
                           ui::EF_LEFT_MOUSE_BUTTON);
    widget->OnMouseEvent(&release);
  }
  void Press(ui::KeyboardCode key) {
    ASSERT_TRUE(ui_test_utils::SendKeyPressSync(browser(), key, false, false,
                                                false, false));
  }
  void Wait(int ms) {
    base::RunLoop loop;
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, loop.QuitClosure(), base::Milliseconds(ms));
    loop.Run();
  }
  void Shoot(const std::string& name) {
    std::optional<std::string> dir =
        base::Environment::Create()->GetVar("AERIE_MUTE_SHOT_DIR");
    if (!dir || dir->empty()) {
      return;
    }
    for (const bool dark : {false, true}) {
      browser_view()->GetWidget()->SetColorModeOverride(
          dark ? ui::ColorProviderKey::ColorMode::kDark
               : ui::ColorProviderKey::ColorMode::kLight);
      Wait(500);
      gfx::NativeWindow window = browser_view()->GetWidget()->GetNativeWindow();
      base::test::TestFuture<scoped_refptr<base::RefCountedMemory>> png;
      ui::GrabWindowSnapshotAsPNG(window, gfx::Rect(window->bounds().size()),
                                  png.GetCallback());
      ASSERT_TRUE(png.Get() && png.Get()->size() > 0);
      ASSERT_TRUE(base::WriteFile(
          base::FilePath::FromUTF8Unsafe(*dir).AppendASCII(
              name + (dark ? "-dark.png" : "-light.png")),
          *png.Get()));
    }
  }

  raw_ptr<content::WebContents> contents_ = nullptr;
};

IN_PROC_BROWSER_TEST_F(AerieSidebarAudioUiTest, ARowWithoutSoundShowsNoMark) {
  EXPECT_EQ(sidebar()->row_at(0)->audio_state(), AudioState::kNone);
  EXPECT_FALSE(sidebar()->row_at(0)->audio_button()->GetVisible());
}

IN_PROC_BROWSER_TEST_F(AerieSidebarAudioUiTest,
                       APlayingTabShowsTheSpeakerAndAMutedTabTheMutedMark) {
  AerieSidebarRowView* row = OpenSoundTab();
  views::ImageButton* mark = row->audio_button();
  EXPECT_TRUE(mark->GetVisible());
  EXPECT_EQ(mark->GetTooltipText(), u"Mute tab");
  EXPECT_EQ(NameOf(mark), u"Mute tab");
  // The mark is a state, not a motion: no layer and no animation, so reduced
  // motion changes nothing.
  EXPECT_FALSE(mark->layer());

  contents_->SetAudioMuted(true);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return row->audio_state() == AudioState::kMuted; }));
  EXPECT_TRUE(mark->GetVisible());
  EXPECT_EQ(mark->GetTooltipText(), u"Unmute tab");
  EXPECT_EQ(NameOf(mark), u"Unmute tab");

  contents_->SetAudioMuted(false);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return row->audio_state() == AudioState::kPlaying; }));
  EXPECT_EQ(mark->GetTooltipText(), u"Mute tab");
}

IN_PROC_BROWSER_TEST_F(AerieSidebarAudioUiTest,
                       ClickingTheMarkMutesAndASecondClickUnmutes) {
  AerieSidebarRowView* row = OpenSoundTab();
  ASSERT_FALSE(contents_->IsAudioMuted());

  Click(row->audio_button());
  EXPECT_TRUE(contents_->IsAudioMuted());
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return row->audio_state() == AudioState::kMuted; }));

  Click(row->audio_button());
  EXPECT_FALSE(contents_->IsAudioMuted());
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return row->audio_state() == AudioState::kPlaying; }));
}

IN_PROC_BROWSER_TEST_F(AerieSidebarAudioUiTest,
                       ClickingTheMarkDoesNotSelectTheTab) {
  AerieSidebarRowView* row = OpenSoundTab();
  ASSERT_EQ(strip()->active_index(), 0);
  Click(row->audio_button());
  EXPECT_TRUE(contents_->IsAudioMuted());
  Wait(200);
  EXPECT_EQ(strip()->active_index(), 0);
  EXPECT_FALSE(row->selected());
}

IN_PROC_BROWSER_TEST_F(AerieSidebarAudioUiTest,
                       TheMarkIsReachedByTabAndSpaceAndEnterToggleIt) {
  AerieSidebarRowView* row = OpenSoundTab();
  views::ImageButton* mark = row->audio_button();
  EXPECT_EQ(mark->GetFocusBehavior(), views::View::FocusBehavior::ALWAYS);

  // The arrow keys of the tree bring focus to the row, as for a user.
  sidebar()->today_tree()->RequestFocus();
  for (size_t i = 0; i < sidebar()->row_count() && focused() != row; ++i) {
    Press(ui::VKEY_DOWN);
  }
  ASSERT_EQ(focused(), row);
  Press(ui::VKEY_TAB);
  ASSERT_TRUE(base::test::RunUntil([&] { return focused() == mark; }));
  // The tab did not become the selected one by the focus.
  EXPECT_EQ(strip()->active_index(), 0);

  Press(ui::VKEY_SPACE);
  ASSERT_TRUE(base::test::RunUntil([&] { return contents_->IsAudioMuted(); }));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return row->audio_state() == AudioState::kMuted; }));
  // The button keeps focus across the change of look.
  EXPECT_EQ(focused(), mark);

  Press(ui::VKEY_RETURN);
  ASSERT_TRUE(base::test::RunUntil([&] { return !contents_->IsAudioMuted(); }));
  EXPECT_EQ(strip()->active_index(), 0);
}

IN_PROC_BROWSER_TEST_F(AerieSidebarAudioUiTest,
                       TheMutedMarkStaysWhileTheTabIsSilentAndUnmutingHidesIt) {
  AerieSidebarRowView* row = OpenSoundTab();
  views::ImageButton* mark = row->audio_button();
  Click(mark);
  ASSERT_TRUE(contents_->IsAudioMuted());
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return row->audio_state() == AudioState::kMuted; }));

  // The page stops; once the tab is no longer recently audible, Chromium's own
  // alerts drop the muted indicator, but the sidebar's mark follows the mute
  // state and stays, so the user can unmute.
  ASSERT_TRUE(content::ExecJs(contents_, "audio.pause(); 0"));
  tabs::TabAlertController* alerts =
      tabs::TabAlertController::From(strip()->GetTabAtIndex(1));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return !alerts->IsAlertActive(tabs::TabAlert::kAudioPlaying);
  }));
  Wait(300);
  EXPECT_EQ(row->audio_state(), AudioState::kMuted);
  EXPECT_TRUE(mark->GetVisible());
  EXPECT_EQ(mark->GetTooltipText(), u"Unmute tab");

  // Unmuting a silent tab: the mark goes, and focus on it moves to the row.
  mark->RequestFocus();
  ASSERT_TRUE(base::test::RunUntil([&] { return focused() == mark; }));
  Press(ui::VKEY_SPACE);
  ASSERT_TRUE(base::test::RunUntil([&] { return !contents_->IsAudioMuted(); }));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return row->audio_state() == AudioState::kNone; }));
  EXPECT_FALSE(mark->GetVisible());
  EXPECT_EQ(focused(), row);
}

// Gallery shots (AERIE_MUTE_SHOT_DIR): the row with the speaker, with the muted
// mark, and with the muted mark focused.
IN_PROC_BROWSER_TEST_F(AerieSidebarAudioUiTest, GalleryShots) {
  AerieSidebarRowView* row = OpenSoundTab();
  Shoot("mute-playing");
  contents_->SetAudioMuted(true);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return row->audio_state() == AudioState::kMuted; }));
  Shoot("mute-muted");
  row->audio_button()->RequestFocus();
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return focused() == row->audio_button(); }));
  Shoot("mute-muted-focused");
  // The same mark once the tab is silent: the look is the same as muted.
  ASSERT_TRUE(content::ExecJs(contents_, "audio.pause(); 0"));
  tabs::TabAlertController* alerts =
      tabs::TabAlertController::From(strip()->GetTabAtIndex(1));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return !alerts->IsAlertActive(tabs::TabAlert::kAudioPlaying);
  }));
  Shoot("mute-muted-silent");
}
