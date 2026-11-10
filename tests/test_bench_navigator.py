"""The navigator pins check (issue #120): judge() against synthetic observations, and the constants that
exist twice, in C++ (components/aerie_navigator_pins) and in the Bench check."""

import copy
import json
import re
import unittest
from pathlib import Path

from aerie_bench.checks.hardening import navigator
from aerie_bench.checks.hardening.navigator import FingerprintingNavigator

REPO = Path(__file__).resolve().parent.parent
HEADER = (REPO / "overlay" / "components" / "aerie_navigator_pins" / "navigator_pins.h").read_text()
FIXTURES = REPO / "tools" / "aerie_bench" / "fixtures" / "hardening"

PINNED_BATTERY = {"charging": True, "level": 1, "chargingTime": 0, "dischargingTime": "Infinity"}
LAPTOP_BATTERY = {"charging": False, "level": 0.57, "chargingTime": "Infinity", "dischargingTime": 9120}
FIVE_PDF = ["PDF Viewer", "Chrome PDF Viewer", "Chromium PDF Viewer", "Microsoft Edge PDF Viewer",
            "WebKit built-in PDF"]
MANY_VOICES = ["eSpeak English", "eSpeak German", "eSpeak French", "eSpeak Spanish"]


def reading(languages=("en-US",), battery=None, voices=("eSpeak English",), **over):
    r = {"languages": list(languages), "battery": dict(battery or PINNED_BATTERY),
         "voices": {"count": len(voices), "names": list(voices), "default": voices[0] if voices else None},
         "devices": 0, "plugins": list(FIVE_PDF), "mimeTypes": ["application/pdf", "text/pdf"],
         "layoutMap": {"size": 48}}
    r.update(over)
    return r


def real(battery=None, voices=tuple(MANY_VOICES), languages=("en-US", "en", "fr")):
    return reading(languages=languages, battery=battery or LAPTOP_BATTERY, voices=voices)


def good():
    on = reading()
    return {"on": {"a": copy.deepcopy(on), "b": copy.deepcopy(on)}, "feature_off": {"a": real()},
            "global_off": {"a": real(languages=("en-US",))}, "site_off": {"a": real(languages=("en-US",)),
                                                                          "b": copy.deepcopy(on)}}


class ConstantsMirrorTest(unittest.TestCase):
    def test_pinned_battery_equals_the_cpp_values(self):
        self.assertIn("kBatteryCharging = true", HEADER)
        self.assertIn("kBatteryLevel = 1.0", HEADER)
        self.assertIn("kBatteryChargingTimeSeconds = 0.0", HEADER)
        self.assertEqual(navigator.PINNED_BATTERY, PINNED_BATTERY)

    def test_the_fixture_page_exists(self):
        self.assertTrue((FIXTURES / "navigator.html").exists())

    def test_the_fixture_reads_every_surface_the_issue_names(self):
        text = (FIXTURES / "navigator.html").read_text()
        for needle in ("navigator.languages", "getVoices", "enumerateDevices", "getBattery", "navigator.plugins",
                       "navigator.mimeTypes", "getLayoutMap"):
            self.assertIn(needle, text)

    def test_the_cpp_hook_is_listed_in_the_series(self):
        series = (REPO / "patches" / "series").read_text()
        self.assertIn("hardening/0021-pin-the-battery-status.patch", series)
        self.assertIn("hardening/0022-limit-speech-voices-to-the-default.patch", series)


class JudgeTest(unittest.TestCase):
    def test_pass(self):
        status, detail = FingerprintingNavigator.judge(good())
        self.assertEqual(status, "pass", detail)
        self.assertIn("languages", detail)
        self.assertIn("battery", detail)
        self.assertIn("voices", detail)

    def test_more_than_one_language_under_protection_fails(self):
        obs = good()
        obs["on"]["a"]["languages"] = ["en-US", "en", "fr"]
        status, detail = FingerprintingNavigator.judge(obs)
        self.assertEqual(status, "fail")
        self.assertIn("languages", detail)

    def test_a_language_that_is_not_the_first_real_one_fails(self):
        obs = good()
        obs["on"]["a"]["languages"] = ["fr"]
        self.assertEqual(FingerprintingNavigator.judge(obs)[0], "fail")

    def test_each_battery_field_is_judged(self):
        for field, value in (("charging", False), ("level", 0.57), ("chargingTime", 30), ("dischargingTime", 9120)):
            obs = good()
            obs["on"]["b"]["battery"][field] = value
            status, detail = FingerprintingNavigator.judge(obs)
            self.assertEqual((status, "battery" in detail), ("fail", True), (field, detail))

    def test_a_laptop_battery_under_protection_fails(self):
        obs = good()
        obs["on"]["a"]["battery"] = dict(LAPTOP_BATTERY)
        self.assertEqual(FingerprintingNavigator.judge(obs)[0], "fail")

    def test_more_than_one_voice_under_protection_fails(self):
        obs = good()
        obs["on"]["a"]["voices"] = {"count": 4, "names": MANY_VOICES, "default": MANY_VOICES[0]}
        status, detail = FingerprintingNavigator.judge(obs)
        self.assertEqual(status, "fail")
        self.assertIn("voices", detail)

    def test_the_kept_voice_must_be_one_the_system_has(self):
        obs = good()
        obs["on"]["a"]["voices"] = {"count": 1, "names": ["Aerie"], "default": "Aerie"}
        status, detail = FingerprintingNavigator.judge(obs)
        self.assertEqual(status, "fail")
        self.assertIn("not a voice of the system", detail)

    def test_no_voices_at_all_is_a_pass_on_a_machine_with_none(self):
        obs = good()
        obs["on"]["a"]["voices"] = obs["on"]["b"]["voices"] = {"count": 0, "names": [], "default": None}
        obs["feature_off"]["a"]["voices"] = {"count": 0, "names": [], "default": None}
        obs["global_off"]["a"]["voices"] = {"count": 0, "names": [], "default": None}
        obs["site_off"]["a"]["voices"] = {"count": 0, "names": [], "default": None}
        status, detail = FingerprintingNavigator.judge(obs)
        self.assertEqual(status, "pass", detail)
        self.assertIn("not distinguishable", detail)

    def test_protection_lost_on_the_other_site_fails(self):
        obs = good()
        obs["site_off"]["b"] = real(languages=("en-US",))
        status, detail = FingerprintingNavigator.judge(obs)
        self.assertEqual(status, "fail")
        self.assertIn("only a.localhost", detail)

    def test_protection_off_must_restore_the_real_battery_and_voices(self):
        for key in ("global_off", "site_off"):
            obs = good()
            obs[key]["a"]["battery"] = dict(PINNED_BATTERY)
            status, detail = FingerprintingNavigator.judge(obs)
            self.assertEqual(status, "fail", key)
            self.assertIn("not the real", detail)
            obs = good()
            obs[key]["a"]["voices"] = {"count": 1, "names": MANY_VOICES[:1], "default": MANY_VOICES[0]}
            status, detail = FingerprintingNavigator.judge(obs)
            self.assertEqual(status, "fail", key)

    def test_a_negative_control_with_one_language_is_inconclusive(self):
        obs = good()
        obs["feature_off"]["a"]["languages"] = ["en-US"]
        status, detail = FingerprintingNavigator.judge(obs)
        self.assertEqual(status, "error")
        self.assertIn("inconclusive", detail)

    def test_a_battery_that_equals_the_pin_is_reported_as_not_distinguishable(self):
        obs = good()
        for where in (("feature_off", "a"), ("global_off", "a"), ("site_off", "a")):
            obs[where[0]][where[1]]["battery"] = dict(PINNED_BATTERY)
        status, detail = FingerprintingNavigator.judge(obs)
        self.assertEqual(status, "pass", detail)
        self.assertIn("battery not distinguishable", detail)

    def test_the_recorded_but_not_pinned_values_never_fail_the_check(self):
        obs = good()
        for key in ("on", "feature_off", "global_off", "site_off"):
            for where in obs[key].values():
                where.update(devices=3, plugins=[], mimeTypes=[], layoutMap={"error": "NotAllowedError"})
        self.assertEqual(FingerprintingNavigator.judge(obs)[0], "pass")

    def test_a_page_error_fails(self):
        obs = good()
        obs["global_off"]["a"] = {"error": "boom"}
        status, detail = FingerprintingNavigator.judge(obs)
        self.assertEqual(status, "fail")
        self.assertIn("boom", detail)

    def test_a_battery_that_could_not_be_read_fails(self):
        obs = good()
        obs["on"]["a"]["battery"] = {"error": "NotSupportedError"}
        status, detail = FingerprintingNavigator.judge(obs)
        self.assertEqual(status, "fail")
        self.assertIn("NotSupportedError", detail)


class RecordedCasesTest(unittest.TestCase):
    def test_the_recorded_file_is_labelled_synthetic(self):
        doc = json.loads((REPO / "tests" / "fixtures" / "hardening" / "hardening_fingerprinting_navigator.json").read_text())
        self.assertEqual(doc["check"], "hardening_fingerprinting_navigator")
        for case in doc["cases"]:
            self.assertTrue(case["name"].startswith(("synthetic_", "negative control")), case["name"])


if __name__ == "__main__":
    unittest.main()
