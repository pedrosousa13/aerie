// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

// Interactive UI tests of "Keep loaded" in the sidebar (issue #267): the tab
// context menu has a check item that shows the state, the Menu key and the
// arrow keys reach it, choosing it shows or hides a small mark in the row, and
// the row's accessible name says it. With AERIE_KEEP_SHOT_DIR set, the last test
// also saves the menu and the mark in light and dark (for the screenshot
// gallery). Run with ./aerie test-cpp --target aerie_interactive_ui_tests.

#include <memory>
#include <optional>
#include <string>

#include "base/environment.h"
#include "base/files/file_util.h"
#include "base/run_loop.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "base/test/run_until.h"
#include "base/test/test_future.h"
#include "chrome/browser/aerie_tab_model/tab_model_service.h"
#include "chrome/browser/aerie_tab_model/tab_model_service_factory.h"
#include "chrome/browser/aerie_tab_model/tab_strip_adapter.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_context_menu.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_row_view.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_view.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"
#include "chrome/grit/generated_resources.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/interactive_test_utils.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/aerie_tab_model/tab_model.h"
#include "content/public/test/browser_test.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/aura/window.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/color/color_provider_key.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/snapshot/snapshot.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/menu/menu_item_view.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"

namespace {

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

}  // namespace

class AerieSidebarKeepLoadedUiTest : public InProcessBrowserTest {
 protected:
  using Command = AerieSidebarContextMenu::Command;

  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));
    ASSERT_TRUE(base::test::RunUntil(
        [&] { return sidebar() && sidebar()->row_count() == 1u; }));
    host_resolver()->AddRule("*", "127.0.0.1");
    ASSERT_TRUE(embedded_test_server()->Start());
    service_ = aerie_tab_model::TabModelServiceFactory::GetForProfile(
        browser()->GetProfile());
    ASSERT_TRUE(service_);
  }

  void TearDownOnMainThread() override {
    service_ = nullptr;
    InProcessBrowserTest::TearDownOnMainThread();
  }

  AerieSidebarView* sidebar() {
    return FindDescendant<AerieSidebarView>(
        BrowserView::GetBrowserViewForBrowser(browser())
            ->vertical_tab_strip_region_view_for_testing());
  }
  TabStripModel* strip() { return browser()->tab_strip_model(); }
  views::View* focused() {
    return BrowserView::GetBrowserViewForBrowser(browser())
        ->GetWidget()
        ->GetFocusManager()
        ->GetFocusedView();
  }

  AerieSidebarRowView* OpenBackgroundTab() {
    const size_t before = sidebar()->row_count();
    EXPECT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
        browser(), embedded_test_server()->GetURL("/title1.html"),
        WindowOpenDisposition::kNewBackgroundTab,
        ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
    EXPECT_TRUE(base::test::RunUntil(
        [&] { return sidebar()->row_count() == before + 1; }));
    return sidebar()->row_at(before);
  }

  void FocusRow(AerieSidebarRowView* row) {
    sidebar()->today_tree()->RequestFocus();
    for (size_t i = 0; i < sidebar()->row_count() && focused() != row; ++i) {
      Press(ui::VKEY_DOWN);
    }
    ASSERT_EQ(focused(), row);
  }

  void Press(ui::KeyboardCode key) {
    ASSERT_TRUE(ui_test_utils::SendKeyPressSync(browser(), key, false, false,
                                                false, false));
  }

  void OpenMenu() {
    Press(ui::VKEY_APPS);
    ASSERT_TRUE(base::test::RunUntil([&] {
      return sidebar()->context_menu() && sidebar()->context_menu()->is_running();
    }));
  }

  // Walks down the open menu with the arrow keys to the item of `command`.
  void WalkTo(Command command) {
    views::MenuItemView* item = sidebar()->context_menu()->item(command);
    ASSERT_TRUE(item);
    for (int i = 0; i < 20 && !item->IsSelected(); ++i) {
      Press(ui::VKEY_DOWN);
    }
    ASSERT_TRUE(item->IsSelected());
  }

  // Opens the menu of the focused row, walks to "Keep loaded" and presses
  // Enter, with the keyboard only.
  void ToggleKeepLoadedWithKeyboard() {
    OpenMenu();
    WalkTo(Command::kKeepLoaded);
    Press(ui::VKEY_RETURN);
    ASSERT_TRUE(base::test::RunUntil(
        [&] { return !sidebar()->context_menu()->is_running(); }));
  }

  std::u16string AccessibleName(AerieSidebarRowView* row) {
    ui::AXNodeData data;
    row->GetViewAccessibility().GetAccessibleNodeData(&data);
    return data.GetString16Attribute(ax::mojom::StringAttribute::kName);
  }

  const aerie_tab_model::TabRecord* RecordOf(int index) {
    const std::string id =
        service_->adapter()->RecordIdForTab(strip()->GetTabAtIndex(index));
    return id.empty() ? nullptr : service_->model()->GetTab(id);
  }

  // Saves `widget` as <dir>/<name>-<mode>.png in light and dark when
  // AERIE_KEEP_SHOT_DIR is set; `before` runs once per mode, after the colour
  // mode is set, and returns the widget to grab.
  template <typename Prepare, typename After>
  void Shoot(const std::string& name, Prepare prepare, After after) {
    std::optional<std::string> dir =
        base::Environment::Create()->GetVar("AERIE_KEEP_SHOT_DIR");
    if (!dir || dir->empty()) {
      return;
    }
    for (const bool dark : {false, true}) {
      BrowserView::GetBrowserViewForBrowser(browser())
          ->GetWidget()
          ->SetColorModeOverride(dark
                                     ? ui::ColorProviderKey::ColorMode::kDark
                                     : ui::ColorProviderKey::ColorMode::kLight);
      Wait(500);
      views::Widget* widget = prepare();
      ASSERT_TRUE(widget);
      widget->SetColorModeOverride(dark
                                       ? ui::ColorProviderKey::ColorMode::kDark
                                       : ui::ColorProviderKey::ColorMode::kLight);
      Wait(500);
      gfx::NativeWindow window = widget->GetNativeWindow();
      base::test::TestFuture<scoped_refptr<base::RefCountedMemory>> png;
      ui::GrabWindowSnapshotAsPNG(
          window, gfx::Rect(window->bounds().size()), png.GetCallback());
      ASSERT_TRUE(png.Get() && png.Get()->size() > 0);
      ASSERT_TRUE(base::WriteFile(
          base::FilePath::FromUTF8Unsafe(*dir).AppendASCII(
              name + (dark ? "-dark.png" : "-light.png")),
          *png.Get()));
      after();
    }
  }

  void Wait(int ms) {
    base::RunLoop loop;
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, loop.QuitClosure(), base::Milliseconds(ms));
    loop.Run();
  }

  raw_ptr<aerie_tab_model::TabModelService> service_ = nullptr;
};

IN_PROC_BROWSER_TEST_F(AerieSidebarKeepLoadedUiTest,
                       TheMenuHasAnUncheckedKeepLoadedItemAndTheRowNoMark) {
  AerieSidebarRowView* row = OpenBackgroundTab();
  FocusRow(row);
  OpenMenu();
  views::MenuItemView* item =
      sidebar()->context_menu()->item(Command::kKeepLoaded);
  ASSERT_TRUE(item);
  EXPECT_EQ(item->GetType(), views::MenuItemView::Type::kCheckbox);
  EXPECT_EQ(item->title(), l10n_util::GetStringUTF16(IDS_AERIE_SIDEBAR_KEEP_LOADED));
  EXPECT_TRUE(sidebar()->context_menu()->IsCommandEnabled(Command::kKeepLoaded));
  EXPECT_FALSE(sidebar()->context_menu()->IsItemChecked(Command::kKeepLoaded));
  EXPECT_FALSE(row->keep_mark()->GetVisible());
  Press(ui::VKEY_ESCAPE);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return !sidebar()->context_menu()->is_running(); }));
  EXPECT_FALSE(RecordOf(1)->keep_loaded);
}

IN_PROC_BROWSER_TEST_F(AerieSidebarKeepLoadedUiTest,
                       KeepLoadedIsReachableByKeyboardAndTogglesTheMark) {
  AerieSidebarRowView* row = OpenBackgroundTab();
  FocusRow(row);
  const std::u16string suffix =
      l10n_util::GetStringFUTF16(IDS_AERIE_SIDEBAR_ROW_KEPT_NAME, u"");
  const std::u16string plain_name = AccessibleName(row);
  EXPECT_FALSE(plain_name.ends_with(suffix));

  ToggleKeepLoadedWithKeyboard();
  EXPECT_TRUE(RecordOf(1)->keep_loaded);
  ASSERT_TRUE(base::test::RunUntil([&] { return row->kept_loaded(); }));
  EXPECT_TRUE(row->keep_mark()->GetVisible());
  // The mark is a state, not a motion: it has no layer and no animation.
  EXPECT_FALSE(row->keep_mark()->layer());
  EXPECT_EQ(AccessibleName(row), plain_name + suffix);
  EXPECT_FALSE(row->keep_mark()->GetTooltipText().empty());

  // The menu now shows the item checked, and the same keys clear it.
  OpenMenu();
  EXPECT_TRUE(sidebar()->context_menu()->IsItemChecked(Command::kKeepLoaded));
  WalkTo(Command::kKeepLoaded);
  Press(ui::VKEY_RETURN);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return !sidebar()->context_menu()->is_running(); }));
  ASSERT_TRUE(base::test::RunUntil([&] { return !row->kept_loaded(); }));
  EXPECT_FALSE(row->keep_mark()->GetVisible());
  EXPECT_FALSE(RecordOf(1)->keep_loaded);
  EXPECT_EQ(AccessibleName(row), plain_name);
}

IN_PROC_BROWSER_TEST_F(AerieSidebarKeepLoadedUiTest,
                       ARowThatIsKeptStaysMarkedWhileOthersAreNot) {
  AerieSidebarRowView* kept = OpenBackgroundTab();
  AerieSidebarRowView* other = OpenBackgroundTab();
  FocusRow(kept);
  ToggleKeepLoadedWithKeyboard();
  ASSERT_TRUE(base::test::RunUntil([&] { return kept->kept_loaded(); }));
  EXPECT_FALSE(other->kept_loaded());
  EXPECT_FALSE(other->keep_mark()->GetVisible());
}

// Gallery shots (AERIE_KEEP_SHOT_DIR): the open menu with the item unchecked
// and checked, and the row with its mark.
IN_PROC_BROWSER_TEST_F(AerieSidebarKeepLoadedUiTest, GalleryShots) {
  AerieSidebarRowView* row = OpenBackgroundTab();
  FocusRow(row);
  auto close_menu = [&] {
    Press(ui::VKEY_ESCAPE);
    ASSERT_TRUE(base::test::RunUntil(
        [&] { return !sidebar()->context_menu()->is_running(); }));
  };
  auto menu_widget = [&]() -> views::Widget* {
    OpenMenu();
    WalkTo(Command::kKeepLoaded);
    return sidebar()->context_menu()->item(Command::kKeepLoaded)->GetWidget();
  };
  Shoot("keep-loaded-menu", menu_widget, close_menu);

  ToggleKeepLoadedWithKeyboard();
  ASSERT_TRUE(base::test::RunUntil([&] { return row->kept_loaded(); }));
  Shoot("keep-loaded-menu-checked", menu_widget, close_menu);
  Shoot(
      "keep-loaded-row",
      [&]() -> views::Widget* {
        return BrowserView::GetBrowserViewForBrowser(browser())->GetWidget();
      },
      [] {});
}
