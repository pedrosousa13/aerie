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
        self.assertIn("widevine_opt_in", c["bench_checks"])

    def test_settings_allowlist_name_equals_the_pref_name(self):
        header = self.read("overlay/components/aerie_widevine/widevine_opt_in.h")
        name = re.search(r'kWidevineEnabled\[\] = "([^"]+)"', header).group(1)
        self.assertIn(f'(*s_allowlist)["{name}"]', self.read("patches/widevine/0003-allow-widevine-pref-in-settings.patch"))
        tbl = json.loads(self.read("tools/aerie_hardening/controls.json"))
        self.assertEqual(next(c for c in tbl["controls"] if c["id"] == "widevine")["pref"]["name"], name)

    def test_ledger_has_a_row_for_the_configurator_patch(self):
        self.assertRegex(self.read("docs/helium-dependency-ledger.md"),
                         r"patches/widevine/0002[^|]*\|[^|]*component-updates\.patch")

    def test_ledger_row_for_the_prefs_hunk_names_the_crash_prefs_patch(self):
        row = next(l for l in self.read("docs/helium-dependency-ledger.md").splitlines()
                   if "patches/widevine/0001" in l)
        self.assertIn("crash-reporting-prefs.patch", row)
        self.assertIn("static_cast<int>(helium::CrashReportingMode::kAsk));", row)

    def test_infobar_enum_patch_names_the_histogram_file(self):
        header = self.read("patches/widevine/0004-ask-before-the-first-drm-request.patch").split("\n---", 1)[0]
        self.assertIn("tools/metrics/histograms/metadata/browser/enums.xml", header)

    def test_only_widevine_goes_to_google_while_opted_in(self):
        patch = self.read("patches/widevine/0002-update-widevine-from-google-when-opted-in.patch")
        for path in ("components/update_client/configurator.h", "components/update_client/update_checker.cc",
                     "components/update_client/ping_manager.cc"):
            self.assertIn(f"+++ b/{path}", patch)
        self.assertIn("config_->ShouldSendToServer(app_id)", patch)
        self.assertIn("config_->ShouldSendToServer(component.app_id)", patch)
        src = self.read("overlay/components/aerie_widevine/widevine_opt_in.cc")
        body = re.search(r"bool ShouldSendToUpdateServer\(.*?\n}\n", src, re.S).group(0)
        self.assertIn("kWidevineAppId", body)

    def test_user_text_does_not_claim_more_than_the_traffic_shows(self):
        for rel in ("overlay/chrome/browser/aerie_widevine/aerie_widevine_prompt.cc",
                    "tools/aerie_hardening/controls.json", "docs/hardening.md"):
            self.assertNotIn("for nothing else", self.read(rel), rel)

    def test_one_gate_for_the_browser_process_and_no_second_dummy_tld(self):
        src = self.read("overlay/components/aerie_widevine/widevine_opt_in.cc")
        self.assertIn("base::NoDestructor<RegistrationGate>", src)
        self.assertNotIn("std::vector<std::unique_ptr<RegistrationGate>>", src)
        self.assertNotIn("DUMMY_SUFFIX", self.read("tools/aerie_bench/checks/widevine_opt_in.py"))

    def test_off_the_record_declines_are_not_written_to_local_state(self):
        prompt = self.read("overlay/chrome/browser/aerie_widevine/aerie_widevine_prompt.cc")
        self.assertIn("IsOffTheRecord()", prompt)
        self.assertRegex(prompt, r"RecordDecline\([^;]*off_the_record_\)")
        src = self.read("overlay/components/aerie_widevine/widevine_opt_in.cc")
        body = re.search(r"void RecordDecline\(.*?\n}\n", src, re.S).group(0)
        self.assertLess(body.index("if (off_the_record)"), body.index("ScopedListPrefUpdate"))

    def test_build_gn_deps_are_sorted_in_the_registration_patch(self):
        patch = self.read("patches/widevine/0001-register-widevine-only-when-opted-in.patch")
        hunk = patch.split("+++ b/chrome/browser/component_updater/BUILD.gn", 1)[1].split("\n--- a/", 1)[0]
        lines = [l[1:].strip() for l in hunk.splitlines() if l[:1] in " +" and l.strip().startswith(('"', '+"', ' "'))]
        deps = [l.strip().strip('+ ,').strip() for l in lines if "//" in l]
        self.assertEqual(deps, sorted(deps, key=lambda d: d.replace('"', '')))


if __name__ == "__main__":
    unittest.main()
