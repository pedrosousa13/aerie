"""The C++ frame paint and check_contrast.py's frame model agree (issue #178).

aerie_frame_unittest.cc pins the minimum contrast of the sidebar text, muted text and accent on the
Vivid and Dusk frame (1440 x 900) to numbers. Those numbers are the output of check_contrast.py. This
test runs the checker and compares, so the unit test cannot drift away from the Python model, and the
pixel-sampling browser test (aerie_frame_browsertest.cc) uses the same numbers.
"""
import pathlib
import re
import subprocess
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent
CHECKER = ROOT / "overlay/components/aerie_design_tokens/check_contrast.py"
UNITTEST = ROOT / "overlay/components/aerie_frame/aerie_frame_unittest.cc"
BROWSERTEST = ROOT / "overlay/chrome/browser/ui/views/aerie_frame/aerie_frame_browsertest.cc"


def checker_minimums(mode):
    out = subprocess.run([sys.executable, str(CHECKER), "-v"], capture_output=True, text=True, check=True).stdout
    match = re.search(rf"ok\s+vivid/{mode}/dusk: ([\d.]+) ([\d.]+) ([\d.]+)", out)
    assert match, out
    return [float(v) for v in match.groups()]


def unittest_minimums(name):
    source = UNITTEST.read_text()
    block = re.search(rf"TEST\(AerieFrameContrastTest, {name}\)(.*?)\n}}", source, re.S).group(1)
    return [float(v) for v in re.findall(r"EXPECT_NEAR\(m\.\w+, ([\d.]+), 0\.05\)", block)]


class FrameContrastGoldenTest(unittest.TestCase):
    def test_unit_test_numbers_are_the_checkers(self):
        for mode, name in (("light", "VividDuskLightMatchesTheCheckerAndMeetsTheMinimums"),
                           ("dark", "VividDuskDarkMatchesTheCheckerAndMeetsTheMinimums")):
            expected = checker_minimums(mode)
            actual = unittest_minimums(name)
            self.assertEqual(len(actual), 3)
            for want, got in zip(expected, actual):
                self.assertAlmostEqual(want, got, delta=0.02, msg=f"{mode}: {expected} vs {actual}")

    def test_the_browser_test_uses_the_same_numbers(self):
        if not BROWSERTEST.exists():
            self.skipTest("browser test not written yet")
        text = BROWSERTEST.read_text()
        for mode in ("light", "dark"):
            for value in checker_minimums(mode):
                self.assertIn(f"{value:.2f}", text)


if __name__ == "__main__":
    unittest.main()
