"""Bench check setup_theme (issue #92): judge() against recorded observations."""

import unittest

from aerie_bench import checks
from aerie_bench.checks.setup_theme import STEP_JS, SetupPageError, SetupTheme, open_look_step

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


class FakeSetupPage:
    """The setup page as the Bench sees it: one step at a time, #next moves on."""

    def __init__(self, steps=("welcome", "import", "look", "block"), stuck=False):
        self.steps, self.step, self.stuck, self.clicks = list(steps), 0, stuck, []

    def navigate_url(self, url):
        self.step = 0

    def wait_for(self, expression, timeout=15.0):
        return True

    def evaluate(self, expression):
        return self.steps[self.step] if expression == STEP_JS else None

    def click(self, selector):
        self.clicks.append(selector)
        if not self.stuck:
            self.step += 1


class OpenLookStepTest(unittest.TestCase):
    def test_presses_continue_past_welcome_and_import(self):
        page = FakeSetupPage()
        open_look_step(page)
        self.assertEqual(page.steps[page.step], "look")
        self.assertEqual(page.clicks, ["#next", "#next"])

    def test_raises_when_the_page_never_reaches_the_theme_step(self):
        with self.assertRaises(SetupPageError):
            open_look_step(FakeSetupPage(stuck=True))


if __name__ == "__main__":
    unittest.main()
