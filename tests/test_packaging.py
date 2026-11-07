"""The Linux desktop entry, app ID and icons say Aerie."""

import configparser
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
LINUX = REPO / "packaging" / "linux"
APP_ID = "me.pedrosousa.Aerie"


def desktop() -> configparser.ConfigParser:
    parser = configparser.ConfigParser(interpolation=None, strict=True)
    parser.optionxform = str
    parser.read(LINUX / "aerie.desktop", encoding="utf-8")
    return parser


class DesktopEntryTest(unittest.TestCase):
    def test_the_entry_names_the_browser_aerie(self):
        entry = desktop()["Desktop Entry"]
        self.assertEqual(entry["Name"], "Aerie")
        self.assertEqual(entry["Icon"], "aerie")
        self.assertEqual(entry["Exec"], "aerie %U")
        self.assertEqual(entry["Type"], "Application")
        self.assertIn("WebBrowser", entry["Categories"])
        self.assertIn("x-scheme-handler/https", entry["MimeType"])

    def test_the_window_class_matches_the_desktop_name_the_browser_reports(self):
        # patches/branding/0003 makes GetDesktopName() return aerie.desktop; Chromium derives the
        # window class from it by dropping ".desktop".
        patch = (REPO / "patches/branding/0003-linux-desktop-identity.patch").read_text()
        self.assertIn('+  return "aerie.desktop";', patch)
        self.assertEqual(desktop()["Desktop Entry"]["StartupWMClass"], "aerie")

    def test_actions_open_aerie_windows(self):
        parser = desktop()
        self.assertEqual(parser["Desktop Entry"]["Actions"], "new-window;new-private-window")
        self.assertEqual(parser["Desktop Action new-window"]["Exec"], "aerie")
        self.assertEqual(parser["Desktop Action new-private-window"]["Exec"], "aerie --incognito")

    def test_nothing_in_it_says_helium_or_chromium(self):
        text = (LINUX / "aerie.desktop").read_text().lower()
        self.assertNotIn("helium", text)
        self.assertNotIn("chromium", text)


class MetainfoTest(unittest.TestCase):
    def setUp(self):
        self.root = ET.parse(LINUX / f"{APP_ID}.metainfo.xml").getroot()

    def test_id_launchable_and_name(self):
        self.assertEqual(self.root.findtext("id"), APP_ID)
        self.assertEqual(self.root.find("launchable").text, "aerie.desktop")
        self.assertEqual(self.root.findtext("name"), "Aerie")

    def test_the_app_id_is_the_installer_rdn(self):
        patch = (REPO / "patches/branding/0003-linux-desktop-identity.patch").read_text()
        self.assertIn(f'+RDN="{APP_ID}"', patch)

    def test_nothing_in_it_says_helium_or_imput(self):
        text = (LINUX / f"{APP_ID}.metainfo.xml").read_text().lower()
        self.assertNotIn("helium", text)
        self.assertNotIn("imput", text)


class InstallScriptTest(unittest.TestCase):
    def run_install(self, prefix: Path) -> subprocess.CompletedProcess:
        return subprocess.run(["sh", str(LINUX / "install.sh"), str(prefix)], capture_output=True, text=True)

    def test_installs_entry_metainfo_and_every_icon_size(self):
        with tempfile.TemporaryDirectory() as tmp:
            prefix = Path(tmp)
            result = self.run_install(prefix)
            self.assertEqual(result.returncode, 0, result.stderr)
            share = prefix / "share"
            self.assertTrue((share / "applications" / "aerie.desktop").is_file())
            self.assertTrue((share / "metainfo" / f"{APP_ID}.metainfo.xml").is_file())
            sizes = sorted(p.parent.parent.name for p in (share / "icons" / "hicolor").glob("*/apps/aerie.png"))
            self.assertEqual(sizes, sorted(f"{s}x{s}" for s in (16, 24, 32, 48, 64, 128, 256)))

    def test_without_a_prefix_it_stops(self):
        result = subprocess.run(["sh", str(LINUX / "install.sh")], capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("usage", result.stderr.lower())


if __name__ == "__main__":
    unittest.main()
