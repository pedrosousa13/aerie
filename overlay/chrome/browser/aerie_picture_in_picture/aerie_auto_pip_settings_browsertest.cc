// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

// Browser test of the Settings switch for automatic picture-in-picture (issue
// #263): Settings > Privacy > Site settings lists "Automatic
// picture-in-picture", its page shows the switch on by default, and the switch
// changes the content setting. Run with ./aerie test-cpp --target
// aerie_browser_tests.

#include <optional>
#include <string>

#include "base/environment.h"
#include "base/files/file_util.h"
#include "base/run_loop.h"
#include "base/task/single_thread_task_runner.h"
#include "base/test/run_until.h"
#include "base/test/test_future.h"
#include "base/time/time.h"
#include "chrome/browser/content_settings/host_content_settings_map_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "ui/base/base_window.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"
#include "components/content_settings/core/common/content_settings.h"
#include "components/content_settings/core/common/content_settings_types.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "ui/aura/window.h"
#include "ui/color/color_provider_key.h"
#include "ui/views/widget/widget.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/snapshot/snapshot.h"
#include "url/gurl.h"

namespace {

// Finds an element in the page and in every shadow tree below it, the way the
// Settings page is built, and waits for it to appear.
constexpr char kDeepHelpers[] = R"(
function deep(root, selector) {
  const hit = root.querySelector(selector);
  if (hit) return hit;
  for (const element of root.querySelectorAll('*')) {
    if (element.shadowRoot) {
      const found = deep(element.shadowRoot, selector);
      if (found) return found;
    }
  }
  return null;
}
function deepText(root) {
  let text = '';
  for (const element of root.querySelectorAll('*')) {
    for (const attribute of ['label', 'page-title']) {
      text += ' ' + (element.getAttribute(attribute) || '');
    }
    if (element.shadowRoot) text += ' ' + deepText(element.shadowRoot);
  }
  return text + ' ' + (root.textContent || '');
}
async function until(check) {
  for (let i = 0; i < 100; ++i) {
    const value = check();
    if (value) return value;
    await new Promise(r => setTimeout(r, 100));
  }
  return null;
}
)";

}  // namespace

class AerieAutoPipSettingsBrowserTest : public InProcessBrowserTest {
 protected:
  content::WebContents* active() {
    return browser()->tab_strip_model()->GetActiveWebContents();
  }
  void Open(const std::string& url) {
    ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), GURL(url)));
  }
  // Runs `script` after the helpers (ExecJs and EvalJs wrap a script in a
  // function, so the helpers go in front of every call).
  content::EvalJsResult Eval(const std::string& script) {
    return content::EvalJs(active(), std::string(kDeepHelpers) + script);
  }
  ContentSetting DefaultSetting() {
    return HostContentSettingsMapFactory::GetForProfile(browser()->GetProfile())
        ->GetDefaultContentSetting(
            ContentSettingsType::AUTO_PICTURE_IN_PICTURE);
  }
  // Runs `body` with `radio` bound to the radio button of the page, once it
  // has appeared.
  content::EvalJsResult OnRadio(const std::string& id, const std::string& body) {
    return Eval("(async () => { const radio = await until(() => deep(document, "
               "'#" + id + "')); if (!radio) return false; " + body +
                " })()");
  }
  // True when the radio button of the page is shown and selected.
  content::EvalJsResult Selected(const std::string& id) {
    return OnRadio(id,
                   "return !radio.hidden && radio.hasAttribute('checked');");
  }
  content::EvalJsResult Shown(const std::string& id) {
    return OnRadio(id, "return !radio.hidden;");
  }
  void Press(const std::string& id) {
    ASSERT_EQ(true, OnRadio(id, "radio.click(); return true;"));
  }
  // With AERIE_AUTOPIP_SHOT_DIR set, saves a screenshot of the window.
  void Shoot(const std::string& name) {
    std::optional<std::string> dir =
        base::Environment::Create()->GetVar("AERIE_AUTOPIP_SHOT_DIR");
    if (!dir || dir->empty()) {
      return;
    }
    // A headless window is small; the shot shows the page at a usual size.
    browser()->GetWindow()->SetBounds(gfx::Rect(0, 0, 1050, 1000));
    gfx::NativeWindow window = browser()->GetWindow()->GetNativeWindow();
    for (const bool dark : {false, true}) {
      views::Widget::GetWidgetForNativeWindow(window)->SetColorModeOverride(
          dark ? ui::ColorProviderKey::ColorMode::kDark
               : ui::ColorProviderKey::ColorMode::kLight);
      // Let the page repaint with the new colours.
      base::RunLoop loop;
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
          FROM_HERE, loop.QuitClosure(), base::Milliseconds(800));
      loop.Run();
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
};

IN_PROC_BROWSER_TEST_F(AerieAutoPipSettingsBrowserTest,
                       TheSiteSettingsListTheCategory) {
  Open("chrome://settings/content");
  EXPECT_EQ(true, Eval("(async () => !!(await until(() => deepText(document)"
                       ".includes('Automatic picture-in-picture') || null)))()"));
}

IN_PROC_BROWSER_TEST_F(AerieAutoPipSettingsBrowserTest,
                       TheSwitchIsOnByDefaultAndTurnsTheSettingOffAndOn) {
  ASSERT_EQ(DefaultSetting(), CONTENT_SETTING_ALLOW);
  Open("chrome://settings/content/autoPictureInPicture");
  // The on option is shown and selected; it sets "allow", not "ask".
  EXPECT_EQ(true, Selected("allowRadioOption"));
  EXPECT_EQ(false, Shown("askRadioOption"));
  Shoot("auto-pip-settings");

  Press("blockRadioOption");
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return DefaultSetting() == CONTENT_SETTING_BLOCK; }));
  EXPECT_EQ(true, Selected("blockRadioOption"));

  Press("allowRadioOption");
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return DefaultSetting() == CONTENT_SETTING_ALLOW; }));
}
