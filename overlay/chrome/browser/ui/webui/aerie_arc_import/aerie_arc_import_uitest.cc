// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

// Interactive UI tests of the Space picker and the result summary of "Import
// from Arc" (issue #171), on the real chrome://settings/people page: a click on
// the row, a fake file chooser that picks a fixture, a keyboard choice in the
// list, the Import button, and the text the page ends up showing. Run with
// `env -u WAYLAND_DISPLAY ./aerie test-cpp --target
// aerie_interactive_ui_tests`.
//
// With AERIE_ARC_SHOT_DIR set, the test also saves a screenshot of the window
// at the picker and at the result, in light and in dark, for the gallery.

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/callback_helpers.h"
#include "base/memory/raw_ptr.h"
#include "base/path_service.h"
#include "base/environment.h"
#include "base/strings/strcat.h"
#include "base/test/run_until.h"
#include "base/test/test_future.h"
#include "chrome/browser/aerie_tab_model/tab_model_service.h"
#include "chrome/browser/aerie_tab_model/tab_model_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/interactive_test_utils.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/aerie_tab_model/tab_model.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/aura/window.h"
#include "ui/shell_dialogs/fake_select_file_dialog.h"
#include "ui/snapshot/snapshot.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace aerie_arc_import {

namespace {

// Finds an element in the page through every shadow root.
constexpr char kDeep[] = R"JS(
  const deep = (root, selector) => {
    const found = root.querySelector(selector);
    if (found) return found;
    for (const element of root.querySelectorAll('*')) {
      if (element.shadowRoot) {
        const inner = deep(element.shadowRoot, selector);
        if (inner) return inner;
      }
    }
    return null;
  };
  const row = () => deep(document, 'settings-aerie-arc-import-row');
  const inRow = selector => {
    const r = row();
    return r && r.shadowRoot ? r.shadowRoot.querySelector(selector) : null;
  };
  const inRowAll = selector => {
    const r = row();
    return r && r.shadowRoot ?
        Array.from(r.shadowRoot.querySelectorAll(selector)) : [];
  };
  const waitFor = async (fn) => {
    for (let i = 0; i < 400; ++i) {
      const value = fn();
      if (value) return value;
      await new Promise(resolve => setTimeout(resolve, 25));
    }
    return null;
  };
)JS";

}  // namespace

class AerieArcImportUiTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    factory_ = ui::FakeSelectFileDialog::RegisterFactory();
    factory_->SetOpenCallback(base::DoNothing());
    service_ = aerie_tab_model::TabModelServiceFactory::GetForProfile(
        browser()->GetProfile());
    ASSERT_TRUE(service_);
    ASSERT_TRUE(base::test::RunUntil([&] { return service_->loaded(); }));
    ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));
    ASSERT_TRUE(ui_test_utils::NavigateToURL(
        browser(), GURL("chrome://settings/people")));
    ASSERT_EQ(true, Js("return !!(await waitFor(() => row()))"));
  }

  content::WebContents* contents() {
    return browser()->tab_strip_model()->GetActiveWebContents();
  }

  // Runs `body` (the body of an async function) with the helpers above.
  content::EvalJsResult Js(const std::string& body) {
    return content::EvalJs(
        contents(),
        base::StrCat({"(async () => {", kDeep, body, "})()"}));
  }

  // Pinned and Favorite records (the window's own first tab is a Today one).
  size_t Arranged() {
    size_t count = 0;
    for (const auto& [id, tab] : service_->model()->tabs()) {
      if (tab.section != aerie_tab_model::Section::kToday) {
        ++count;
      }
    }
    return count;
  }

  // Empties the sidebar of the Pinned and Favorite tabs and the folders an
  // import made. A second import of the same Space skips the tabs that are
  // already there (issue #276), so a test that imports it twice resets between
  // the passes.
  void ResetArranged() {
    std::vector<std::string> tabs;
    for (const auto& [id, tab] : service_->model()->tabs()) {
      if (tab.section != aerie_tab_model::Section::kToday) {
        tabs.push_back(id);
      }
    }
    for (const std::string& id : tabs) {
      service_->model()->RemoveTab(id);
    }
    std::vector<std::string> folders;
    for (const auto& [id, folder] : service_->model()->folders()) {
      folders.push_back(id);
    }
    for (const std::string& id : folders) {
      service_->model()->RemoveFolder(id);
    }
    ASSERT_EQ(Arranged(), 0u);
  }

  base::FilePath Fixture(const std::string& name) {
    base::FilePath root;
    EXPECT_TRUE(base::PathService::Get(base::DIR_SRC_TEST_DATA_ROOT, &root));
    return root.AppendASCII("components/aerie_tab_model/testdata/arc")
        .AppendASCII(name);
  }

  // Presses "Import from Arc" and picks `fixture` in the chooser.
  void OpenWith(const std::string& fixture) {
    ASSERT_EQ(true, Js("inRow('#aerieArcImportTrigger').click(); return true;"));
    ASSERT_TRUE(base::test::RunUntil(
        [&] { return factory_->GetLastDialog() != nullptr; }));
    ASSERT_TRUE(factory_->GetLastDialog()->CallFileSelected(Fixture(fixture),
                                                            "json"));
  }

  void SetDark(bool dark) {
    BrowserView::GetBrowserViewForBrowser(browser())
        ->GetWidget()
        ->SetColorModeOverride(dark ? ui::ColorProviderKey::ColorMode::kDark
                                    : ui::ColorProviderKey::ColorMode::kLight);
  }

  // Saves a screenshot of the window when AERIE_ARC_SHOT_DIR is set.
  void Shoot(const std::string& name) {
    std::optional<std::string> dir =
        base::Environment::Create()->GetVar("AERIE_ARC_SHOT_DIR");
    if (!dir || dir->empty()) {
      return;
    }
    // Let the page settle: two frames.
    ASSERT_EQ(true, Js("await new Promise(r => requestAnimationFrame(() => "
                       "requestAnimationFrame(r))); return true;"));
    gfx::NativeWindow window = BrowserView::GetBrowserViewForBrowser(browser())
                                   ->GetWidget()
                                   ->GetNativeWindow();
    base::test::TestFuture<scoped_refptr<base::RefCountedMemory>> png;
    ui::GrabWindowSnapshotAsPNG(
        window, gfx::Rect(window->bounds().size()), png.GetCallback());
    ASSERT_TRUE(png.Get() && png.Get()->size() > 0);
    ASSERT_TRUE(base::WriteFile(
        base::FilePath::FromUTF8Unsafe(*dir).AppendASCII(name + ".png"),
        *png.Get()));
  }

  raw_ptr<ui::FakeSelectFileDialog::Factory, DanglingUntriaged> factory_ =
      nullptr;
  raw_ptr<aerie_tab_model::TabModelService, DanglingUntriaged> service_ =
      nullptr;
};

IN_PROC_BROWSER_TEST_F(AerieArcImportUiTest, ThePickerListsEachSpaceByNameAndIcon) {
  OpenWith("three_spaces.json");
  ASSERT_EQ(true, Js("return !!(await waitFor(() => "
                     "inRow('#aerieArcImportSpaces')));"));
  // Three Spaces, by name, with the Space Arc did not name called "Space 3".
  EXPECT_EQ(Js(R"JS(return inRowAll('.aerie-arc-name')
      .map(e => e.textContent.trim()).join('|');)JS"),
            "Work|Home|Space 3");
  EXPECT_EQ(Js(R"JS(return inRowAll('.aerie-arc-icon')[0]
      .textContent.trim();)JS"),
            "\xF0\x9F\x8F\x9B");
  EXPECT_EQ(Js(R"JS(return inRowAll('.aerie-arc-space .secondary')
      .map(e => e.textContent.trim()).join('|');)JS"),
            "2 pinned, 0 folders, 1 Favorites|1 pinned, 0 folders, "
            "1 Favorites|0 pinned, 0 folders, 1 Favorites");
  // The first Space is chosen.
  EXPECT_EQ(Js("return inRowAll('input[name=aerieArcSpace]')"
               ".map(e => e.checked).join();"),
            "true,false,false");
  // Nothing is imported yet.
  EXPECT_EQ(Arranged(), 0u);

  // The arrow key moves the choice to the next Space.
  ASSERT_EQ(true, Js("inRowAll('input[name=aerieArcSpace]')[0].focus(); "
                     "return true;"));
  content::SimulateKeyPress(contents(), ui::DomKey::ARROW_DOWN,
                            ui::DomCode::ARROW_DOWN, ui::VKEY_DOWN, false,
                            false, false, false);
  EXPECT_EQ(Js("return inRowAll('input[name=aerieArcSpace]')"
               ".map(e => e.checked).join();"),
            "false,true,false");

  for (bool dark : {false, true}) {
    SetDark(dark);
    Shoot(dark ? "picker-dark" : "picker-light");
  }
  SetDark(false);
  // Cancel closes the dialog and imports nothing.
  ASSERT_EQ(true, Js("inRow('#aerieArcImportCancel').click(); "
                     "return !!(await waitFor(() => "
                     "!inRow('#aerieArcImportDialog')));"));
  EXPECT_EQ(Arranged(), 0u);
}

IN_PROC_BROWSER_TEST_F(AerieArcImportUiTest, TheResultSaysWhatCameInAndWhatDidNot) {
  for (bool dark : {false, true}) {
    SetDark(dark);
    OpenWith("three_spaces.json");
    ASSERT_EQ(true, Js("return !!(await waitFor(() => "
                       "inRow('#aerieArcImportSpaces')));"));
    // The second Space, "Home".
    ASSERT_EQ(true, Js("inRowAll('input[name=aerieArcSpace]')[1].click(); "
                       "return true;"));
    ASSERT_EQ(true, Js("inRow('#aerieArcImportConfirm').click(); "
                       "return !!(await waitFor(() => "
                       "inRow('#aerieArcImportResult')));"));
    EXPECT_EQ(Js(R"JS(return inRow('#aerieArcImportResult ul')
        .textContent.replace(/\s+/g, ' ').trim();)JS"),
              "Pinned tabs: 1 Folders: 0 Favorites: 1 Custom names kept: 0");
    EXPECT_EQ(Js(R"JS(return inRow('#aerieArcImportSkipped')
        .textContent.replace(/\s+/g, ' ').trim();)JS"),
              "Little Arc windows: 1");
    const std::string text = Js(R"JS(return inRow('#aerieArcImportResult')
        .textContent.replace(/\s+/g, ' ');)JS").ExtractString();
    EXPECT_THAT(text, testing::HasSubstr("Not imported"));
    EXPECT_THAT(text, testing::HasSubstr("Boosts, Easels, Little Arc, Peek and "
                                         "Space themes are not imported."));
    EXPECT_THAT(text, testing::HasSubstr("Logins are not imported."));
    Shoot(dark ? "result-dark" : "result-light");
    ASSERT_EQ(true, Js("inRow('#aerieArcImportDone').click(); "
                       "return !!(await waitFor(() => "
                       "!inRow('#aerieArcImportDialog')));"));
    // Each pass imports the Space into an empty sidebar.
    EXPECT_EQ(Arranged(), 2u);
    ResetArranged();
  }
  SetDark(false);
}

IN_PROC_BROWSER_TEST_F(AerieArcImportUiTest, TitlesFromTheFileAreTextNotMarkup) {
  OpenWith("markup_titles.json");
  ASSERT_EQ(true, Js("return !!(await waitFor(() => "
                     "inRow('#aerieArcImportSpaces')));"));
  EXPECT_EQ(Js("return inRowAll('.aerie-arc-name')[0].textContent.trim();"),
            "<svg onload=alert(3)>");
  // The dialog has no element that the file's text could have made.
  EXPECT_EQ(Js(R"JS(return inRow('#aerieArcImportSpaces')
      .querySelectorAll('svg, img, script, b, [onload], [onerror]')
      .length;)JS"),
            0);
  ASSERT_EQ(true, Js("inRow('#aerieArcImportConfirm').click(); "
                     "return !!(await waitFor(() => "
                     "inRow('#aerieArcImportResult')));"));
  EXPECT_EQ(Js(R"JS(return inRow('#aerieArcImportDialog')
      .querySelectorAll('svg[onload], img, script, b').length;)JS"),
            0);
}

IN_PROC_BROWSER_TEST_F(AerieArcImportUiTest, AFileThatFailsShowsTheReasonAndChangesNothing) {
  OpenWith("not_an_object.json");
  ASSERT_EQ(true, Js("return !!(await waitFor(() => "
                     "inRow('#aerieArcImportError')));"));
  EXPECT_EQ(Js("return inRow('#aerieArcImportError').textContent.trim();"),
            "That file does not look like an Arc sidebar "
            "(StorableSidebar.json).");
  // No list to pick from, and no Import button.
  EXPECT_EQ(Js("return !!inRow('#aerieArcImportSpaces') || "
               "!!inRow('#aerieArcImportConfirm');"),
            false);
  Shoot("error-light");
  ASSERT_EQ(true, Js("inRow('#aerieArcImportDone').click(); "
                     "return !!(await waitFor(() => "
                     "!inRow('#aerieArcImportDialog')));"));
  EXPECT_EQ(Arranged(), 0u);
}

}  // namespace aerie_arc_import
