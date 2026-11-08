"""The setup page's Neutral theme is the default and is the plain, untinted theme (issue #92).

Neutral clears the user colour and sets the grayscale pref, Chromium's grey default. The check proves it
at run time on a built browser:

* ``window.aerieSetup.state.color`` is ``neutral`` on a fresh page, and Neutral is the first swatch.
* The frame colour in chrome://theme/colors.css (``--color-sys-header``) under Neutral is near grey: its
  channels are within ``NEUTRAL_MAX_SPREAD`` of each other. Chromium's grey palette is slightly cool, so
  the limit is not zero. A coloured theme (Alpenglow) is read the same way as a control and must be
  tinted, so a measure that cannot see a tint fails the check.
* Neutral keeps the appearance: with Dark chosen, Neutral's colours differ from Neutral's under Light.

``observe`` drives the browser and ``judge`` turns the data into a verdict, as the hardening checks do.
"""

from __future__ import annotations

import json
import re
import time

from ..session import Session
from . import register
from .hardening.common import HardeningCheck, Verdict

SETUP_URL = "chrome://setup/"
COLORS_URL = "chrome://theme/colors.css?sets=ui,chrome"
SETTLE_SECONDS = 1.0  # the theme change reaches the colour provider asynchronously

STATE_JS = """(() => ({
  color: window.aerieSetup && window.aerieSetup.state.color,
  mode: window.aerieSetup && window.aerieSetup.state.mode,
  swatches: [...document.querySelectorAll('.sw')].map(b => b.dataset.val),
}))()"""


def theme_colors(session: Session) -> str:
    """The theme's colour variables as the browser serves them now."""
    session.navigate_url(COLORS_URL)
    return str(session.evaluate("document.documentElement.innerText") or "")


NEUTRAL_MAX_SPREAD = 16  # largest gap between the red, green and blue channels of a "grey"
HEADER_VAR = re.compile(r"--color-sys-header:\s*(#[0-9a-fA-F]{6}(?:[0-9a-fA-F]{2})?|rgba?\([^)]*\))")


def header_spread(colors_css: str) -> int | None:
    """Gap between the strongest and weakest channel of the frame colour, or None if it is not there."""
    match = HEADER_VAR.search(colors_css)
    if not match:
        return None
    value = match.group(1)
    if value.startswith("#"):
        channels = [int(value[i:i + 2], 16) for i in (1, 3, 5)]
    else:
        channels = [round(float(n)) for n in re.findall(r"[\d.]+", value)[:3]]
    return max(channels) - min(channels)


LOOK_STEP = "look"
STEP_JS = "(() => { const s = window.aerieSetup && window.aerieSetup.state; return s ? s.steps[s.step] : null; })()"


class SetupPageError(RuntimeError):
    pass


def open_look_step(session: Session, max_clicks: int = 4) -> None:
    """Open the setup page and press its Continue button until the theme step shows.

    The page opens on the welcome step and renders one step at a time, so the theme step's buttons
    exist only after the welcome and import steps are passed, as a person does."""
    session.navigate_url(SETUP_URL)
    if not session.wait_for("!!window.aerieSetup", timeout=20):
        raise SetupPageError("the setup page did not start (no window.aerieSetup)")
    for _ in range(max_clicks):
        step = session.evaluate(STEP_JS)
        if step == LOOK_STEP:
            session.wait_for("document.querySelectorAll('.sw').length > 0", timeout=10)
            return
        # The page renders a step after its state changes, so the button can be missing for a moment.
        if not session.wait_for("!!document.querySelector('#next')", timeout=10):
            raise SetupPageError(f"the setup page has no Continue button on the {step!r} step")
        session.click("#next")
        session.wait_for(f"{STEP_JS} !== {json.dumps(step)}", timeout=10)
    raise SetupPageError(f"the setup page did not reach the {LOOK_STEP!r} step (at {session.evaluate(STEP_JS)!r})")


def choose(session: Session, group: str, value: str) -> None:
    """Pick a mode or colour on the setup page's theme step, the way a click does."""
    open_look_step(session)
    session.click(f'[data-group="{group}"][data-val="{value}"]')
    time.sleep(SETTLE_SECONDS)


@register
class SetupTheme(HardeningCheck):
    name = "setup_theme"
    control = "all"
    negative_control = ("Alpenglow is read the same way as Neutral and must be tinted, so a measure that cannot "
                        "see a tint, or a Neutral that is tinted, is reported.")

    def observe(self, session: Session) -> dict:
        open_look_step(session)
        obs = {"initial": session.evaluate(STATE_JS)}
        choose(session, "mode", "light")
        choose(session, "color", "alpenglow")
        obs["alpenglow"] = theme_colors(session)
        choose(session, "color", "neutral")
        obs["neutral_light"] = theme_colors(session)
        choose(session, "mode", "dark")
        choose(session, "color", "neutral")
        obs["neutral_dark"] = theme_colors(session)
        return obs

    @staticmethod
    def judge(obs: dict) -> Verdict:
        initial = obs.get("initial") or {}
        if initial.get("color") != "neutral":
            return "fail", f"the setup page starts on {initial.get('color')!r}, not 'neutral'"
        if (initial.get("swatches") or [None])[0] != "neutral":
            return "fail", f"Neutral is not the first swatch (order: {initial.get('swatches')})"
        for key in ("alpenglow", "neutral_light", "neutral_dark"):
            if len(obs.get(key) or "") < 100:
                return "fail", f"the theme colours for {key} could not be read"
        spreads = {key: header_spread(obs[key]) for key in ("alpenglow", "neutral_light", "neutral_dark")}
        if None in spreads.values():
            return "fail", f"the frame colour (--color-sys-header) is missing from the theme colours: {spreads}"
        if spreads["alpenglow"] <= NEUTRAL_MAX_SPREAD:
            return "fail", f"the tint control (Alpenglow) reads as grey (spread {spreads['alpenglow']}), so the check cannot see a tint"
        for key in ("neutral_light", "neutral_dark"):
            if spreads[key] > NEUTRAL_MAX_SPREAD:
                return "fail", f"Neutral is tinted ({key} frame colour spread {spreads[key]}, limit {NEUTRAL_MAX_SPREAD})"
        if obs["neutral_dark"] == obs["neutral_light"]:
            return "fail", "Neutral does not follow the light and dark appearance"
        return "pass", "Neutral is first and the default, untinted, and follows light and dark"
