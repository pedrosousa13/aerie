"""Per-site cosmetic hiding (issue #101): "Hide leftover ad boxes" off for one site, network blocking still on.

A second browser starts on a profile whose ``AerieFilterLists/state.json`` says cosmetic hiding is off for ``a.localhost``
(the field the Guard panel's switch saves). The fixture page (``fixtures/cosmetic/site-switch.html``) has an ad box that
EasyList's generic rule hides, and a script whose file name a generic EasyList rule blocks. It is opened as two sites:

* ``a.localhost``: the box stays visible (it has a layout box and its ``display`` is not ``none``) and the ad script is
  still blocked (DevTools ``loadingFailed`` with ``net::ERR_BLOCKED_BY_CLIENT``);
* ``b.localhost``: the box is hidden, and the ad script is blocked too. This is the control: it proves the engine does hide
  the box on this page, so an ``a.localhost`` box that is shown is shown because of the switch and not because nothing hides.

The check fails when the policy callback (``FilterEngineService::SetCosmeticFilteringPolicy``, installed by the filter list
manager) is missing: the box on ``a.localhost`` is then hidden like on ``b.localhost``. ``judge`` is plain logic that the tests
run on recorded observations, among them that case.
"""

from __future__ import annotations

import json
import time
from pathlib import Path

from ..session import CheckResult, Session
from . import register
from .ad_blocking import collect_outcomes
from .cosmetic_filtering import _is_hidden, _is_shown, measure_script

PAGE = "/cosmetic/site-switch.html"
OFF_HOST = "a.localhost"
ON_HOST = "b.localhost"
BOX = ".ad-slot"
CONTENT = "#content"
AD_PATH = "/cosmetic/ad_header.js"  # EasyList: /ad_header.js$script
STATE_FILE = Path("AerieFilterLists") / "state.json"
SEEDED_STATE = {"cosmetic_off": [OFF_HOST]}
SETTLE_SECONDS = 20.0
EXTRA_QUIET_SECONDS = 3.0
EVENT_METHODS = ("Network.requestWillBeSent", "Network.responseReceived", "Network.loadingFailed")


def seed_state(profile_path: Path) -> Path:
    """Write the state file that turns cosmetic hiding off for OFF_HOST. Returns its path."""
    path = Path(profile_path) / STATE_FILE
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(SEEDED_STATE))
    return path


def judge_visit(host: str, visit: dict | None, box_should_hide: bool, problems: list[str]) -> None:
    """One page visit: ``visit`` has ``box`` and ``content`` (measurements) and ``ad`` (the outcome of the ad script)."""
    if not visit:
        problems.append(f"{host}: the page was not observed")
        return
    box, content, ad = visit.get("box"), visit.get("content"), visit.get("ad")
    if not box or not box.get("found"):
        problems.append(f"{host}: the ad box is not on the page")
    elif box_should_hide and not _is_hidden(box):
        problems.append(f"{host}: the ad box is NOT HIDDEN (display {box['display']}, {box['width']}x{box['height']}), "
                        "so the engine hides nothing on this page and the check proves nothing")
    elif not box_should_hide and not _is_shown(box):
        problems.append(f"{host}: the ad box is HIDDEN BUT MUST STAY (display {box['display']}, "
                        f"{box['width']}x{box['height']}): cosmetic hiding is off for this site")
    if not content or not content.get("found") or not _is_shown(content):
        problems.append(f"{host}: first-party content is missing or hidden")
    state = (ad or {}).get("state")
    if state != "blocked":
        problems.append(f"{host}: the ad script was not blocked (outcome {state or 'never requested'}): "
                        "network blocking must stay on")


def judge(obs: dict | None) -> tuple[str, str, dict]:
    """Return (status, detail, data). ``obs``: ``seeded`` (the state file was written), ``off`` and ``on`` (visits)."""
    if not obs:
        return "fail", "no observations: the second browser did not run", {}
    problems: list[str] = []
    if not obs.get("seeded"):
        problems.append("the state file was not seeded")
    judge_visit(OFF_HOST, obs.get("off"), False, problems)
    judge_visit(ON_HOST, obs.get("on"), True, problems)
    data = {k: obs.get(k) for k in ("seeded", "off", "on")}
    if problems:
        return "fail", f"{len(problems)} problem(s): " + "; ".join(problems), data
    return "pass", (f"cosmetic hiding off for {OFF_HOST}: its ad box stayed visible and its ad request was still blocked; "
                    f"on {ON_HOST} the box was hidden and the request blocked"), data


def _outcome(session: Session, host: str) -> dict:
    events: list[dict] = []
    for method in EVENT_METHODS:
        events += session.take_events(method)
    order = {m: i for i, m in enumerate(EVENT_METHODS)}
    url = f"http://{host}:{session.fixtures.port}{AD_PATH}"
    return collect_outcomes(sorted(events, key=lambda e: order.get(e.get("method"), 3))).get(url, {})


@register
class CosmeticSiteSwitch:
    name = "cosmetic_site_switch"
    seeds_helium_services = False

    def __init__(self, settle_seconds: float = SETTLE_SECONDS):
        self.settle_seconds = settle_seconds

    def _measure(self, session: Session) -> dict:
        found = session.evaluate(measure_script([BOX, CONTENT])) or []
        by = {m["selector"]: m for m in found}
        return {"box": by.get(BOX), "content": by.get(CONTENT)}

    def _visit(self, session: Session, host: str, wait_for_hide: bool, quiet: float = 0.0) -> dict:
        session.cdp("Network.enable")
        for method in EVENT_METHODS:  # Drop what the last page left.
            session.take_events(method)
        nav = session.navigate_url(f"http://{host}:{session.fixtures.port}{PAGE}")
        if nav["error"]:
            return {"error": nav["error"]}
        start = time.monotonic()
        deadline = start + self.settle_seconds
        while True:
            seen = self._measure(session)
            if (wait_for_hide and seen["box"] and _is_hidden(seen["box"])) or time.monotonic() > deadline:
                break
            time.sleep(0.2)
        # A hide that comes late must still be seen on the site that keeps its box.
        time.sleep(quiet)
        seen = self._measure(session)
        seen["ad"] = _outcome(session, host)
        seen["seconds"] = round(time.monotonic() - start, 1)
        return seen

    def run(self, session: Session) -> CheckResult:
        with session.new_profile() as profile:
            seeded = seed_state(profile.path).is_file()
            with session.variant(profile=profile) as browser:
                # b first: how long the engine takes to hide the box tells how long a late hide on a would take.
                on = self._visit(browser, ON_HOST, wait_for_hide=True)
                quiet = max(EXTRA_QUIET_SECONDS, (on.get("seconds") or 0) + 2.0)
                off = self._visit(browser, OFF_HOST, wait_for_hide=False, quiet=quiet)
                browser.screenshot("cosmetic_site_switch.png")
        status, detail, data = judge({"seeded": seeded, "off": off, "on": on})
        return CheckResult(self.name, status, detail, data=data)
