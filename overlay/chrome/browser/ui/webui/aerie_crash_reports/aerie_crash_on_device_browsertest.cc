// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

// A forced renderer crash leaves a local report (issue #262, docs/crash-reports.md): the
// dump is in the crash database of the test profile, mode 0600, nothing is in completed/
// (no report was handled as sent), and the database's upload switch is off.

#include <sys/stat.h>

#include <memory>
#include <optional>
#include <string>

#include "base/base_switches.h"
#include "base/command_line.h"
#include "base/environment.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/path_service.h"
#include "base/test/run_until.h"
#include "base/time/time.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/crash/core/app/crashpad.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/content_switches.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/no_renderer_crashes_assertion.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/public/common/chrome_debug_urls.h"

namespace aerie_crash_reports {

namespace {

int CountFiles(const base::FilePath& dir, const std::string& pattern) {
  int n = 0;
  base::FileEnumerator files(dir, false, base::FileEnumerator::FILES, pattern);
  for (base::FilePath p = files.Next(); !p.empty(); p = files.Next()) {
    ++n;
  }
  return n;
}

}  // namespace

class AerieCrashOnDeviceBrowserTest : public InProcessBrowserTest {
 public:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    command_line->AppendSwitch(switches::kEnableCrashReporterForTesting);
  }
};

IN_PROC_BROWSER_TEST_F(AerieCrashOnDeviceBrowserTest, ARendererCrashLeavesALocalReport) {
  std::optional<base::FilePath> database = crash_reporter::GetCrashpadDatabasePath();
  ASSERT_TRUE(database.has_value()) << "crashpad is not initialized in this test binary";
  // The test harness points XDG_CONFIG_HOME at a folder of its own. Never run against a
  // real profile's folder: the database must be under that folder, and it must not be the
  // user's own ~/.config.
  std::unique_ptr<base::Environment> env = base::Environment::Create();
  std::optional<std::string> xdg = env->GetVar("XDG_CONFIG_HOME");
  ASSERT_TRUE(xdg.has_value()) << "no XDG_CONFIG_HOME";
  const base::FilePath xdg_dir(*xdg);
  ASSERT_TRUE(xdg_dir.IsParent(*database)) << database->value();
  base::FilePath home;
  ASSERT_TRUE(base::PathService::Get(base::DIR_HOME, &home));
  ASSERT_NE(xdg_dir, home.AppendASCII(".config")) << "that is the user's own folder";
  base::FilePath pending = database->Append("pending");
  const int before = CountFiles(pending, "*.dmp");
  // Older runs of other builds may have left files in completed/. This run must add none.
  const int completed_before = CountFiles(database->Append("completed"), "*");

  content::ScopedAllowRendererCrashes allow_crashes;
  content::WebContents* tab = browser()->tab_strip_model()->GetActiveWebContents();
  content::RenderProcessHostWatcher watcher(
      tab->GetPrimaryMainFrame()->GetProcess(),
      content::RenderProcessHostWatcher::WATCH_FOR_PROCESS_EXIT);
  tab->GetController().LoadURL(GURL(blink::kChromeUICrashURL), content::Referrer(),
                               ui::PAGE_TRANSITION_TYPED, std::string());
  watcher.Wait();

  // The handler writes the dump after the process is gone.
  ASSERT_TRUE(base::test::RunUntil([&] { return CountFiles(pending, "*.dmp") > before; }))
      << "no dump appeared in " << pending.value();
  base::FileEnumerator dumps(pending, false, base::FileEnumerator::FILES, "*.dmp");
  for (base::FilePath p = dumps.Next(); !p.empty(); p = dumps.Next()) {
    struct stat st;
    ASSERT_EQ(0, stat(p.value().c_str(), &st));
    EXPECT_EQ(0600, st.st_mode & 0777) << p.value();
  }
  EXPECT_EQ(completed_before, CountFiles(database->Append("completed"), "*"));
}

}  // namespace aerie_crash_reports
