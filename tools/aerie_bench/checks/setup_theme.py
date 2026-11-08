"""The setup page's Neutral theme is the default and differs from Graphite (issue #92).

Neutral clears the user colour and the grayscale pref (the baseline colour source). Graphite sets the
grayscale pref. The check proves it at run time on a built browser:

* ``window.aerieSetup.state.color`` is ``neutral`` on a fresh page, and Neutral is the first swatch.
* After the page sends Graphite and then Neutral, the browser's theme colours (chrome://theme/colors.css)
  differ between the two.
* Neutral keeps the appearance: with Dark chosen, Neutral's colours differ from Neutral's under Light.

``observe`` drives the browser and ``judge`` turns the data into a verdict, as the hardening checks do.
"""

from __future__ import annotations

import json
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


LOOK_STEP = "look"
STEP_JS = "(() => { const s = window.aerieSetup && window.aerieSetup.state; return s ? s.steps[s.step] : null; })()"


class SetupPageError(RuntimeError):
    pass


def open_look_step(session: Session, max_clicks: int = 4) -> None:
    """Open the setup page and press its Continue button until the theme step shows.

    The page opens on the welcome step and renders one step at a time, so the theme step's buttons
    exist only after the welcome and import steps are passed, as a person does."""
    session.navigate_url(SETUP_URL)
    session.wait_for("!!window.aerieSetup", timeout=20)
    for _ in range(max_clicks):
        step = session.evaluate(STEP_JS)
        if step == LOOK_STEP:
            session.wait_for("document.querySelectorAll('.sw').length > 0", timeout=10)
            return
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
    negative_control = ("Graphite and Neutral are read the same way, and the check fails if their colours are "
                        "equal, so a Neutral that fell back to grayscale is reported.")

    def observe(self, session: Session) -> dict:
        open_look_step(session)
        obs = {"initial": session.evaluate(STATE_JS)}
        choose(session, "mode", "light")
        choose(session, "color", "graphite")
        obs["graphite"] = theme_colors(session)
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
        for key in ("graphite", "neutral_light", "neutral_dark"):
            if len(obs.get(key) or "") < 100:
                return "fail", f"the theme colours for {key} could not be read"
        if obs["neutral_light"] == obs["graphite"]:
            return "fail", "choosing Neutral gives the same theme colours as Graphite"
        if obs["neutral_dark"] == obs["neutral_light"]:
            return "fail", "Neutral does not follow the light and dark appearance"
        return "pass", "Neutral is first and the default, differs from Graphite and follows light and dark"
