"""Crash reports stay on the device (issue #262): source checks. The C++ and TypeScript are built and run
by `./aerie test-cpp`."""

import json
import re
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
HANDLER_DIR = REPO / "overlay/chrome/browser/ui/webui/aerie_crash_reports"
PAGE_DIR = REPO / "overlay/chrome/browser/resources/settings/aerie_crash_reports_page"
SERIES = [l.strip() for l in (REPO / "patches/series").read_text().splitlines()
          if l.strip() and not l.startswith("#")]


def code(path: Path) -> str:
    return re.sub(r"//[^\n]*", "", path.read_text(encoding="utf-8"))


class HandlerTest(unittest.TestCase):
    def setUp(self):
        self.src = code(HANDLER_DIR / "aerie_crash_reports_handler.cc")

    def test_four_messages_and_none_takes_a_path(self):
        names = re.findall(r'"(aerieCrashReports\.\w+)"', self.src)
        self.assertEqual(sorted(set(names)),
                         ["aerieCrashReports.delete", "aerieCrashReports.deleteAll",
                          "aerieCrashReports.list", "aerieCrashReports.openFolder"])
        self.assertNotRegex(self.src, r"FilePath::FromUTF8|FilePath\(args|ReadFile|ReadFileToString")

    def test_the_answer_has_no_contents_and_no_path(self):
        self.assertNotRegex(self.src, r'Set\("(path|file|contents|data|dump)"')
        for key in ("id", "time", "processType", "version"):
            self.assertIn(f'"{key}"', self.src)

    def test_the_page_never_reads_a_dump_itself(self):
        for f in PAGE_DIR.glob("*.ts"):
            self.assertNotRegex(code(f), r"\.dmp|fetch\(|XMLHttpRequest")


class StringsTest(unittest.TestCase):
    def test_every_message_is_known_to_the_page_and_the_provider(self):
        grdp = (HANDLER_DIR / "aerie_crash_reports_strings.grdp").read_text()
        ids = re.findall(r'name="(IDS_SETTINGS_AERIE_CRASH_REPORTS_\w+)"', grdp)
        self.assertGreaterEqual(len(ids), 19)
        provider = (REPO / "patches/crash/0003-add-the-crash-reports-settings-page.patch").read_text()
        for i in ids:
            self.assertIn(i, provider, i)


class PatchesTest(unittest.TestCase):
    def test_the_patches_are_in_the_series(self):
        self.assertIn("crash/0002-keep-crash-reports-on-the-device.patch", SERIES)
        self.assertIn("crash/0003-add-the-crash-reports-settings-page.patch", SERIES)
        for p in SERIES:
            if p.startswith("crash/000"):
                self.assertTrue((REPO / "patches" / p).is_file(), p)

    def test_the_upload_is_removed_in_the_patch(self):
        text = (REPO / "patches/crash/0002-keep-crash-reports-on-the-device.patch").read_text()
        self.assertIn("SetUploadsEnabled(false)", text)
        self.assertRegex(text, r"(?m)^-.*DeletePendingReports")

    def test_the_doc_exists_and_is_linked(self):
        self.assertTrue((REPO / "docs/crash-reports.md").is_file())
        self.assertIn("crash-reports.md", (REPO / "docs/network-endpoints.md").read_text())


if __name__ == "__main__":
    unittest.main()
