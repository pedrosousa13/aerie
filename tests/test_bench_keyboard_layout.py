"""The keyboard layout check (issue #203): judge() against the recorded observations, and the fixture page."""

import json
import unittest
from pathlib import Path

from aerie_bench.checks.hardening.keyboard_layout import KeyboardLayout

REPO = Path(__file__).resolve().parent.parent
RECORDED = REPO / "tests" / "fixtures" / "hardening" / "hardening_fingerprinting_keyboard_layout.json"


def cases() -> dict[str, dict]:
    doc = json.loads(RECORDED.read_text())
    assert doc["check"] == "hardening_fingerprinting_keyboard_layout"
    return {c["name"]: c for c in doc["cases"]}


class RecordedCasesTest(unittest.TestCase):
    def test_every_recorded_case_judges_as_recorded(self):
        for name, case in cases().items():
            with self.subTest(case=name):
                status, detail = KeyboardLayout.judge(case["obs"])
                self.assertEqual(status, case["expect"], detail)
                self.assertIn(case["detail_contains"], detail)

    def test_an_unpatched_build_fails_because_the_map_is_not_refused(self):
        status, _ = KeyboardLayout.judge(cases()["unpatched_build_returns_the_map"]["obs"])
        self.assertEqual(status, "fail")

    def test_the_pinned_case_is_recorded_as_a_pass_with_both_negative_controls(self):
        names = set(cases())
        self.assertIn("refused_when_on_real_when_off", names)
        self.assertIn("refusal_still_there_when_pref_off", names)
        self.assertIn("refusal_still_there_when_site_off", names)
        self.assertIn("negative_control_rejects", names)


class FixturePageTest(unittest.TestCase):
    def test_the_page_calls_get_layout_map_and_reports_lock_and_unlock(self):
        text = (REPO / "tools" / "aerie_bench" / "fixtures" / "hardening" / "keyboard-layout.html").read_text()
        self.assertIn("getLayoutMap()", text)
        self.assertIn("typeof keyboard.lock", text)
        self.assertIn("typeof keyboard.unlock", text)
        self.assertIn("window.__done = true", text)


if __name__ == "__main__":
    unittest.main()
