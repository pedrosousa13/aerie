"""Bench check setup_theme (issue #92): judge() against recorded observations."""

import unittest

from aerie_bench import checks
from aerie_bench.checks.setup_theme import STEP_JS, SetupPageError, SetupTheme, open_look_step

def css(header):
    return f"--color-sys-header: {header};" + "--color-sys-primary: #3551d2;" * 10


LIGHT = css("#e0e2eb")
DARK = css("#1c1e22")
TINTED = css("#f7c1cd")


def obs(**over):
    base = {"initial": {"color": "neutral", "mode": "system", "swatches": ["neutral", "alpenglow"]},
            "alpenglow": TINTED, "neutral_light": LIGHT, "neutral_dark": DARK}
    base.update(over)
    return base


class SetupThemeTest(unittest.TestCase):
    def test_is_registered(self):
        self.assertIs(checks.get("setup_theme"), SetupTheme)

    def test_passes_when_neutral_is_default_untinted_and_follows_the_mode(self):
        self.assertEqual(SetupTheme.judge(obs())[0], "pass")

    def test_fails_when_the_default_is_not_neutral(self):
        self.assertEqual(SetupTheme.judge(obs(initial={"color": "alpenglow", "swatches": ["neutral"]}))[0], "fail")

    def test_fails_when_neutral_is_not_first(self):
        self.assertEqual(SetupTheme.judge(obs(initial={"color": "neutral", "swatches": ["sky", "neutral"]}))[0], "fail")

    def test_fails_when_neutral_is_tinted(self):
        self.assertEqual(SetupTheme.judge(obs(neutral_light=TINTED))[0], "fail")

    def test_fails_when_the_tint_control_is_not_tinted(self):
        # a colour theme that reads as grey means the measure cannot see a tint
        self.assertEqual(SetupTheme.judge(obs(alpenglow=LIGHT))[0], "fail")

    def test_reads_rgb_notation_too(self):
        self.assertEqual(SetupTheme.judge(obs(neutral_light=css("rgb(224, 226, 235)")))[0], "pass")
        self.assertEqual(SetupTheme.judge(obs(neutral_light=css("rgb(247, 193, 205)")))[0], "fail")

    def test_fails_when_neutral_ignores_the_mode(self):
        self.assertEqual(SetupTheme.judge(obs(neutral_dark=LIGHT))[0], "fail")

    def test_fails_when_colours_cannot_be_read(self):
        self.assertEqual(SetupTheme.judge(obs(alpenglow=""))[0], "fail")


class FakeSetupPage:
    """The setup page as the Bench sees it: one step at a time, #next moves on.

    With ``slow`` the step's button exists only after something has waited for it, as on a page that
    is still rendering: a click before that finds no element."""

    def __init__(self, steps=("welcome", "import", "look", "block"), stuck=False, slow=False, never=False):
        self.steps, self.step, self.stuck, self.clicks = list(steps), 0, stuck, []
        self.slow, self.never, self.next_exists = slow, never, not slow

    def navigate_url(self, url):
        self.step = 0
        self.next_exists = not self.slow

    def wait_for(self, expression, timeout=15.0, interval=0.25):
        if "#next" in expression:
            if self.never:
                return None
            self.next_exists = True
        return True

    def evaluate(self, expression):
        return self.steps[self.step] if expression == STEP_JS else None

    def click(self, selector):
        if selector == "#next" and not self.next_exists:
            raise RuntimeError(f"click: no element matches {selector!r}")
        self.clicks.append(selector)
        if not self.stuck:
            self.step += 1
            self.next_exists = not self.slow


class OpenLookStepTest(unittest.TestCase):
    def test_presses_continue_past_welcome_and_import(self):
        page = FakeSetupPage()
        open_look_step(page)
        self.assertEqual(page.steps[page.step], "look")
        self.assertEqual(page.clicks, ["#next", "#next"])

    def test_waits_for_the_continue_button_before_clicking_it(self):
        page = FakeSetupPage(slow=True)
        open_look_step(page)
        self.assertEqual(page.steps[page.step], "look")

    def test_raises_when_the_continue_button_never_appears(self):
        with self.assertRaises(SetupPageError):
            open_look_step(FakeSetupPage(slow=True, never=True))

    def test_raises_when_the_page_never_reaches_the_theme_step(self):
        with self.assertRaises(SetupPageError):
            open_look_step(FakeSetupPage(stuck=True))


if __name__ == "__main__":
    unittest.main()
