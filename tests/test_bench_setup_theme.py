"""Bench check setup_theme (issue #92): judge() against recorded observations."""

import unittest

from aerie_bench import checks
from aerie_bench.checks.setup_theme import SetupTheme

GRAY = "--color-sys-primary: #5f6368;" * 10
LIGHT = "--color-sys-primary: #e4e5ea;" * 10
DARK = "--color-sys-primary: #202127;" * 10


def obs(**over):
    base = {"initial": {"color": "neutral", "mode": "system", "swatches": ["neutral", "alpenglow"]},
            "graphite": GRAY, "neutral_light": LIGHT, "neutral_dark": DARK}
    base.update(over)
    return base


class SetupThemeTest(unittest.TestCase):
    def test_is_registered(self):
        self.assertIs(checks.get("setup_theme"), SetupTheme)

    def test_passes_when_neutral_is_default_distinct_and_follows_the_mode(self):
        self.assertEqual(SetupTheme.judge(obs())[0], "pass")

    def test_fails_when_the_default_is_not_neutral(self):
        self.assertEqual(SetupTheme.judge(obs(initial={"color": "alpenglow", "swatches": ["neutral"]}))[0], "fail")

    def test_fails_when_neutral_is_not_first(self):
        self.assertEqual(SetupTheme.judge(obs(initial={"color": "neutral", "swatches": ["sky", "neutral"]}))[0], "fail")

    def test_fails_when_neutral_equals_graphite(self):
        self.assertEqual(SetupTheme.judge(obs(neutral_light=GRAY))[0], "fail")

    def test_fails_when_neutral_ignores_the_mode(self):
        self.assertEqual(SetupTheme.judge(obs(neutral_dark=LIGHT))[0], "fail")

    def test_fails_when_colours_cannot_be_read(self):
        self.assertEqual(SetupTheme.judge(obs(graphite=""))[0], "fail")


if __name__ == "__main__":
    unittest.main()
