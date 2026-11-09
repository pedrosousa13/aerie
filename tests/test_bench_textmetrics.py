"""The measureText check (issue #118): judge() against synthetic observations (hand-written, never a
recording), and the noise range that exists twice, in C++ (components/aerie_text_metrics) and in the check."""

import copy
import re
import unittest
from pathlib import Path

from aerie_bench.checks.hardening import textmetrics
from aerie_bench.checks.hardening.textmetrics import FingerprintingTextMetrics

REPO = Path(__file__).resolve().parent.parent
HEADER = (REPO / "overlay" / "components" / "aerie_text_metrics" / "text_metrics_noise.h").read_text()

REAL = [92.5, 118.25, 41.0, 203.75]
LAYOUT = {"stickyTop": 0, "firstRow": 400, "rowHeight": 24, "rects_exact": True}


def reading(widths, **over):
    r = {"widths": list(widths), "layout": dict(LAYOUT)}
    r.update(over)
    return r


def scaled(factor):
    return reading([w * factor for w in REAL])


def good():
    return {"on": {"a1": scaled(1.0000007), "a2": scaled(1.0000007), "b": scaled(0.9999991)},
            "feature_off": {"a": reading(REAL), "b": reading(REAL)},
            "global_off": {"a": reading(REAL)},
            "site_off": {"a": reading(REAL), "b": scaled(1.0000004)}}


class RangeMirrorTest(unittest.TestCase):
    def test_the_check_range_equals_the_cpp_range(self):
        cpp = float(re.search(r"kMaxDeviation = ([0-9.e-]+);", HEADER).group(1))
        self.assertEqual(cpp, textmetrics.MAX_DEVIATION)

    def test_the_fixture_page_exists(self):
        self.assertTrue((REPO / "tools" / "aerie_bench" / "fixtures" / "hardening" / "textmetrics.html").exists())

    def test_the_feature_flag_names_the_new_feature(self):
        self.assertEqual(textmetrics.FEATURE_FLAG, "--disable-features=HeliumNoiseTextMetrics")


class JudgeTest(unittest.TestCase):
    def test_pass(self):
        status, detail = FingerprintingTextMetrics.judge(good())
        self.assertEqual(status, "pass", detail)

    def test_two_loads_of_one_site_must_be_equal(self):
        # What a per-document factor (the Bromite patch's base::RandDouble) shows.
        obs = good()
        obs["on"]["a2"] = scaled(0.9999993)
        status, detail = FingerprintingTextMetrics.judge(obs)
        self.assertEqual(status, "fail")
        self.assertIn("two loads of the same site", detail)

    def test_two_sites_must_differ(self):
        obs = good()
        obs["on"]["b"] = copy.deepcopy(obs["on"]["a1"])
        status, detail = FingerprintingTextMetrics.judge(obs)
        self.assertEqual(status, "fail")
        self.assertIn("same widths on a.localhost and b.localhost", detail)

    def test_protection_on_must_change_the_widths(self):
        obs = good()
        for key in ("a1", "a2"):
            obs["on"][key] = reading(REAL)
        obs["on"]["b"] = scaled(0.9999991)
        status, detail = FingerprintingTextMetrics.judge(obs)
        self.assertEqual(status, "fail")
        self.assertIn("unnoised", detail)

    def test_noise_beyond_the_range_fails(self):
        obs = good()
        obs["on"]["a1"] = obs["on"]["a2"] = scaled(1.001)
        status, detail = FingerprintingTextMetrics.judge(obs)
        self.assertEqual(status, "fail")
        self.assertIn("more than", detail)

    def test_each_control_must_restore_the_real_widths(self):
        for key, where in (("global_off", "a"), ("site_off", "a")):
            obs = good()
            obs[key][where] = scaled(1.0000007)
            status, detail = FingerprintingTextMetrics.judge(obs)
            self.assertEqual(status, "fail", key)
            self.assertIn("not the real widths", detail)

    def test_the_other_site_must_stay_noised_when_one_is_off(self):
        obs = good()
        obs["site_off"]["b"] = reading(REAL)
        status, detail = FingerprintingTextMetrics.judge(obs)
        self.assertEqual(status, "fail")
        self.assertIn("only a.localhost", detail)

    def test_a_control_whose_sites_differ_means_the_negative_control_failed(self):
        obs = good()
        obs["feature_off"]["b"] = reading([w + 1 for w in REAL])
        status, detail = FingerprintingTextMetrics.judge(obs)
        self.assertEqual(status, "fail")
        self.assertIn("negative control failed", detail)

    def test_a_layout_that_misbehaves_fails(self):
        for key, value in (("stickyTop", 3), ("firstRow", 399), ("rowHeight", 25), ("rects_exact", False)):
            obs = good()
            obs["on"]["a1"]["layout"][key] = value
            status, detail = FingerprintingTextMetrics.judge(obs)
            self.assertEqual(status, "fail", key)
            self.assertIn("layout", detail)

    def test_a_page_error_fails(self):
        obs = good()
        obs["global_off"]["a"] = {"error": "boom"}
        status, detail = FingerprintingTextMetrics.judge(obs)
        self.assertEqual(status, "fail")
        self.assertIn("boom", detail)


if __name__ == "__main__":
    unittest.main()
