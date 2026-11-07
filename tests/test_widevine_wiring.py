"""What the repo says about Widevine (#7): overlay defaults, series, table row, ledger row.

The real build is the compile check; these catch the wiring a reviewer would look for.
"""

import json
import re
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent


class RepoWiringTest(unittest.TestCase):
    """The overlay and patches say what the issue says. The real build is the compile check."""

    def read(self, rel):
        return (REPO / rel).read_text()

    def test_pref_defaults_are_off_and_empty(self):
        src = self.read("overlay/components/aerie_widevine/widevine_opt_in.cc")
        self.assertRegex(src, r"RegisterBooleanPref\(\s*kWidevineEnabled,\s*false\s*\)")
        self.assertRegex(src, r"RegisterListPref\(\s*kDeclinedSites\s*\)")

    def test_prompt_never_shows_when_enabled_or_declined(self):
        src = self.read("overlay/components/aerie_widevine/widevine_opt_in.cc")
        body = re.search(r"bool ShouldPrompt\(.*?\n}\n", src, re.S).group(0)
        for part in ("IsEnabled", "IsDeclined", "empty()"):
            self.assertIn(part, body)

    def test_series_lists_the_widevine_patches_and_they_exist(self):
        series = [l.strip() for l in self.read("patches/series").splitlines() if l.strip() and not l.startswith("#")]
        wvp = [s for s in series if s.startswith("widevine/")]
        self.assertGreaterEqual(len(wvp), 4)
        for s in wvp:
            self.assertTrue((REPO / "patches" / s).is_file(), s)

    def test_no_patch_makes_the_cdm_download_url_a_literal_helium_host(self):
        for p in (REPO / "patches" / "widevine").glob("*.patch"):
            self.assertNotRegex(p.read_text(), r"imput\.net|helium\.computer")

    def test_widevine_control_is_off_by_default_in_the_table(self):
        tbl = json.loads(self.read("tools/aerie_hardening/controls.json"))
        c = next(c for c in tbl["controls"] if c["id"] == "widevine")
        self.assertEqual(c["pref"], {"name": "aerie.widevine.enabled", "default": False})
        self.assertEqual(c["ui"]["kind"], "toggle")
        self.assertIn("hardening_pref_defaults", c["bench_checks"])

    def test_settings_allowlist_name_equals_the_pref_name(self):
        header = self.read("overlay/components/aerie_widevine/widevine_opt_in.h")
        name = re.search(r'kWidevineEnabled\[\] = "([^"]+)"', header).group(1)
        self.assertIn(f'(*s_allowlist)["{name}"]', self.read("patches/widevine/0003-allow-widevine-pref-in-settings.patch"))
        tbl = json.loads(self.read("tools/aerie_hardening/controls.json"))
        self.assertEqual(next(c for c in tbl["controls"] if c["id"] == "widevine")["pref"]["name"], name)

    def test_ledger_has_a_row_for_the_configurator_patch(self):
        self.assertRegex(self.read("docs/helium-dependency-ledger.md"),
                         r"patches/widevine/0002[^|]*\|[^|]*component-updates\.patch")


if __name__ == "__main__":
    unittest.main()
