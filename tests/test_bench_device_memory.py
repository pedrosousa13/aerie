"""The device memory check (issue #115): judge() against the recorded observations."""

import json
import unittest
from pathlib import Path

from aerie_bench.checks.hardening import device_memory
from aerie_bench.checks.hardening.device_memory import CAP, DeviceMemory, parse_hint

RECORDED = Path(__file__).parent / "fixtures" / "hardening" / "hardening_device_memory.json"


def cases() -> dict[str, dict]:
    doc = json.loads(RECORDED.read_text())
    assert doc["check"] == "hardening_device_memory"
    return {c["name"]: c for c in doc["cases"]}


class RecordedCasesTest(unittest.TestCase):
    def test_every_recorded_case_judges_as_recorded(self):
        for name, case in cases().items():
            with self.subTest(case=name):
                status, detail = DeviceMemory.judge(case["obs"])
                self.assertEqual(status, case["expect"], detail)
                self.assertIn(case["detail_contains"], detail)

    def test_the_four_branches_are_all_recorded(self):
        by_status = {c["expect"] for c in cases().values()}
        self.assertEqual(by_status, {"pass", "fail", "error"})
        names = cases()
        self.assertEqual(DeviceMemory.judge(names["capped_with_protection_on_off_and_site_off"]["obs"])[0], "pass")
        self.assertEqual(DeviceMemory.judge(names["small_machine_is_inconclusive"]["obs"])[0], "error")
        self.assertIn("inconclusive", DeviceMemory.judge(names["eight_gb_machine_is_inconclusive"]["obs"])[1])
        self.assertIn("negative control failed",
                      DeviceMemory.judge(names["negative_control_still_capped_header"]["obs"])[1])

    def test_a_missing_window_value_is_a_failure_not_inconclusive(self):
        for name in ("window_api_missing", "negative_control_api_missing"):
            status, detail = DeviceMemory.judge(cases()[name]["obs"])
            self.assertEqual(status, "fail", name)
            self.assertNotIn("inconclusive", detail)

    def test_the_legacy_header_and_the_site_off_variant_are_judged(self):
        for name, text in (("legacy_header_not_capped", "Device-Memory header 32"),
                           ("legacy_header_never_sent", "no Device-Memory header"),
                           ("cap_lost_when_site_off", "site switched off"),
                           ("negative_control_legacy_header_missing", "negative control failed"),
                           ("negative_control_legacy_header_still_capped", "negative control failed")):
            status, detail = DeviceMemory.judge(cases()[name]["obs"])
            self.assertEqual(status, "fail", name)
            self.assertIn(text, detail)

    def test_cap_is_defined_once(self):
        self.assertEqual(CAP, 8.0)
        self.assertEqual(parse_hint("8"), 8.0)
        self.assertIsNone(parse_hint(None))
        self.assertIsNone(parse_hint("x"))

    def test_site_off_exception_uses_the_helium_noise_content_setting(self):
        prefs = device_memory.site_off_prefs("a.localhost:1234")
        exc = prefs["profile"]["content_settings"]["exceptions"]["helium_noise"]
        self.assertEqual(exc, {"http://a.localhost:1234,*": {"setting": 2}})


if __name__ == "__main__":
    unittest.main()
