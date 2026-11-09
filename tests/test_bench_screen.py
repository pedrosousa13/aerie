"""The screen size check (issue #116): judge() against recorded observations, and the bucket list that
exists twice, in C++ (components/aerie_screen) and in the Bench check. Keep them equal."""

import copy
import re
import unittest
from pathlib import Path

from aerie_bench.checks.hardening import screen
from aerie_bench.checks.hardening.screen import BUCKETS, FingerprintingScreen, bucket_for

REPO = Path(__file__).resolve().parent.parent
CC = (REPO / "overlay" / "components" / "aerie_screen" / "screen_size.cc").read_text()


def reading(width, height, outer=(1500, 900), **over):
    r = {"width": width, "height": height, "availWidth": width, "availHeight": height, "availLeft": 0, "availTop": 0,
         "colorDepth": 24, "pixelDepth": 24, "isExtended": False, "screenX": 0, "screenY": 0,
         "outerWidth": outer[0], "outerHeight": outer[1], "innerWidth": outer[0], "innerHeight": outer[1] - 100,
         "devicePixelRatio": 1, "mqWidth": True, "mqHeight": True}
    r.update(over)
    return r


def real(**over):
    # A real display that is none of the common sizes, with a taskbar and a window offset.
    return reading(1600, 900, availHeight=870, availTop=30, colorDepth=30, pixelDepth=30, screenX=40, screenY=60, **over)


def good():
    on = reading(1680, 1050)
    return {"on": {"a": copy.deepcopy(on), "b": copy.deepcopy(on)},
            "feature_off": {"a": real()}, "global_off": {"a": real()},
            "site_off": {"a": real(), "b": copy.deepcopy(on)}}


class BucketsMirrorTest(unittest.TestCase):
    def test_bench_buckets_equal_the_cpp_buckets(self):
        body = re.search(r"kBuckets\[\] = \{(.*?)\};", CC, re.S).group(1)
        self.assertEqual(tuple((int(w), int(h)) for w, h in re.findall(r"\{(\d+),\s*(\d+)\}", body)), BUCKETS)

    def test_the_list_is_the_one_the_issue_names(self):
        self.assertEqual(BUCKETS, ((1280, 800), (1366, 768), (1440, 900), (1680, 1050), (1920, 1080),
                                   (2560, 1440), (3840, 2160)))

    def test_bucket_choice(self):
        self.assertEqual(bucket_for(1500, 900), (1680, 1050))
        self.assertEqual(bucket_for(1280, 800), (1280, 800))
        self.assertEqual(bucket_for(1281, 800), (1440, 900))
        self.assertEqual(bucket_for(5120, 2880), (5120, 2880))

    def test_the_fixture_page_exists(self):
        self.assertTrue((REPO / "tools" / "aerie_bench" / "fixtures" / "hardening" / "screen.html").exists())

    def test_the_window_flag_matches_the_window(self):
        self.assertEqual(screen.WINDOW_FLAG, "--window-size=1500,900")
        self.assertNotIn(screen.WINDOW, BUCKETS)


class JudgeTest(unittest.TestCase):
    def test_pass(self):
        status, detail = FingerprintingScreen.judge(good())
        self.assertEqual(status, "pass", detail)
        self.assertIn("1680x1050", detail)

    def test_the_real_screen_under_protection_fails(self):
        obs = good()
        obs["on"]["a"] = real()
        status, detail = FingerprintingScreen.judge(obs)
        self.assertEqual(status, "fail")
        self.assertIn("not the 1680x1050 common size", detail)

    def test_each_protected_field_is_judged(self):
        for field, value, text in (("availWidth", 1600, "available area"), ("availTop", 30, "available area"),
                                   ("screenX", 5, "window position"), ("screenY", 5, "window position"),
                                   ("colorDepth", 30, "colour depth"), ("pixelDepth", 30, "colour depth"),
                                   ("isExtended", True, "isExtended"), ("mqWidth", False, "media feature"),
                                   ("mqHeight", False, "media feature"), ("devicePixelRatio", 2, "devicePixelRatio")):
            obs = good()
            obs["on"]["a"][field] = value
            status, detail = FingerprintingScreen.judge(obs)
            self.assertEqual((status, text in detail), ("fail", True), (field, detail))

    def test_a_screen_smaller_than_the_window_fails(self):
        obs = good()
        obs["on"]["a"].update(outerWidth=1700, outerHeight=900)  # bucket for 1700x900 is 1920x1080, not 1680x1050
        self.assertEqual(FingerprintingScreen.judge(obs)[0], "fail")

    def test_protection_lost_on_the_other_site_fails(self):
        obs = good()
        obs["site_off"]["b"] = real()
        status, detail = FingerprintingScreen.judge(obs)
        self.assertEqual(status, "fail")
        self.assertIn("only a.localhost", detail)

    def test_protection_off_must_restore_the_real_screen(self):
        for key, where in (("global_off", "a"), ("site_off", "a")):
            obs = good()
            obs[key][where] = reading(1680, 1050)
            status, detail = FingerprintingScreen.judge(obs)
            self.assertEqual(status, "fail", key)
            self.assertIn("not the real one", detail)

    def test_protection_off_may_put_the_window_somewhere_else(self):
        # Each variant is a new browser, and headless does not always place its window at the same spot.
        obs = good()
        obs["global_off"]["a"].update(screenX=10, screenY=10)
        status, detail = FingerprintingScreen.judge(obs)
        self.assertEqual(status, "pass", detail)

    def test_a_negative_control_that_is_still_protected_is_inconclusive(self):
        obs = good()
        obs["feature_off"]["a"] = reading(1680, 1050)
        status, detail = FingerprintingScreen.judge(obs)
        self.assertEqual(status, "error")
        self.assertIn("inconclusive", detail)

    def test_a_negative_control_whose_media_feature_disagrees_fails(self):
        obs = good()
        obs["feature_off"]["a"]["mqWidth"] = False
        status, detail = FingerprintingScreen.judge(obs)
        self.assertEqual(status, "fail")
        self.assertIn("negative control failed", detail)

    def test_a_page_error_fails(self):
        obs = good()
        obs["global_off"]["a"] = {"error": "boom"}
        status, detail = FingerprintingScreen.judge(obs)
        self.assertEqual(status, "fail")
        self.assertIn("boom", detail)


if __name__ == "__main__":
    unittest.main()
