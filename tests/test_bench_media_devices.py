"""The media devices check (issue #202): judge() against the recorded observations, and the fixture page."""

import json
import unittest
from pathlib import Path

from aerie_bench.checks.hardening.media_devices import GRANT_FLAGS, KINDS, MediaDevices, blank, counts, shape

REPO = Path(__file__).resolve().parent.parent
RECORDED = REPO / "tests" / "fixtures" / "hardening" / "hardening_fingerprinting_media_devices.json"


def cases() -> dict[str, dict]:
    doc = json.loads(RECORDED.read_text())
    assert doc["check"] == "hardening_fingerprinting_media_devices"
    return {c["name"]: c for c in doc["cases"]}


class RecordedCasesTest(unittest.TestCase):
    def test_every_recorded_case_judges_as_recorded(self):
        for name, case in cases().items():
            with self.subTest(case=name):
                status, detail = MediaDevices.judge(case["obs"])
                self.assertEqual(status, case["expect"], detail)
                self.assertIn(case["detail_contains"], detail)

    def test_an_unpatched_build_fails_on_a_machine_without_a_camera(self):
        # Without the patch the stock list has no videoinput here, so the first browser lists two entries.
        status, detail = MediaDevices.judge(cases()["unpatched_build_lists_the_machine_without_a_camera"]["obs"])
        self.assertEqual(status, "fail")
        self.assertIn("2 entries", detail)
        self.assertIn("0 videoinput", detail)

    def test_a_machine_that_already_lists_every_kind_is_never_a_pass(self):
        status, _ = MediaDevices.judge(cases()["machine_with_every_device_is_inconclusive"]["obs"])
        self.assertEqual(status, "error")


class HelperTest(unittest.TestCase):
    def test_blank_and_counts(self):
        self.assertTrue(blank({"kind": "audioinput", "deviceId": "", "label": "", "groupId": ""}))
        self.assertFalse(blank({"kind": "audioinput", "deviceId": "x", "label": "", "groupId": ""}))
        self.assertEqual(counts([{"kind": "videoinput"}]), {"audioinput": 0, "audiooutput": 0, "videoinput": 1})

    def test_shape_ignores_order(self):
        a, b = {"kind": "audioinput"}, {"kind": "videoinput"}
        self.assertEqual(shape([a, b]), shape([b, a]))

    def test_the_grant_variant_has_a_fake_camera_and_a_granted_permission(self):
        self.assertIn("--use-fake-ui-for-media-stream", GRANT_FLAGS)
        self.assertIn("--use-fake-device-for-media-stream", GRANT_FLAGS)
        self.assertEqual(KINDS, ("audioinput", "audiooutput", "videoinput"))


class FixturePageTest(unittest.TestCase):
    def test_the_page_enumerates_devices_and_asks_for_a_grant_only_on_request(self):
        text = (REPO / "tools" / "aerie_bench" / "fixtures" / "hardening" / "media-devices.html").read_text()
        self.assertIn("enumerateDevices()", text)
        self.assertIn("getUserMedia", text)
        self.assertIn("get('gum') === '1'", text)
        self.assertIn("window.__done = true", text)


if __name__ == "__main__":
    unittest.main()
