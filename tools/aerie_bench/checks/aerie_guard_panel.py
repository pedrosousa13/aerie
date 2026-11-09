"""Aerie Guard panel (issue #100): the page loaded straight into a tab has no captured tab.

There is no bubble here, so the handler has no target. The check proves, on the real browser:

1. ``chrome.send`` is undefined on ``chrome://aerie-guard.top-chrome``.
2. The page's own Mojo handler answers ``getState`` with null (no site).
3. A ``setGuardOn(false)`` call through that handler changes nothing in ``AerieFilterLists/state.json``
   and no exception appears for the host of any open tab.

``judge`` is plain logic that the tests run on recorded observations, including a handler that takes a
host from the page (the negative control: the check must fail against it).
"""

from __future__ import annotations

import time
from pathlib import Path

from ..session import CheckResult, Session
from . import register

PANEL_URL = "chrome://aerie-guard.top-chrome/"
STATE_FILE = "AerieFilterLists/state.json"
IMPORT_PROBE = """(async () => {
  const m = await import('./browser_proxy.js');
  const p = m.BrowserProxy.getInstance();
  const first = await p.handler.getState();
  p.handler.setGuardOn(false);
  await new Promise(r => setTimeout(r, 1500));
  const second = await p.handler.getState();
  return {chromeSend: typeof (window.chrome && window.chrome.send),
          firstState: first.state, secondState: second.state};
})()"""


def judge(obs: dict | None) -> list[str]:
    """Return the problems. ``obs``: chromeSend, firstState, secondState, state_before, state_after."""
    if not obs:
        return ["no observations: the panel page did not load"]
    problems: list[str] = []
    if obs.get("chromeSend") != "undefined":
        problems.append(f"chrome.send is {obs.get('chromeSend')!r} on the panel page, expected undefined")
    for key in ("firstState", "secondState"):
        if obs.get(key) is not None:
            problems.append(f"the handler of a panel with no captured tab answered with a site ({key}: {obs[key]})")
    before, after = obs.get("state_before"), obs.get("state_after")
    if after != before:
        problems.append("a switch call from the page changed state.json")
    if after and any(h in after for h in obs.get("hosts", [])):
        problems.append("an exception for a host the page never named appeared in state.json")
    return problems


@register
class AerieGuardPanel:
    name = "aerie_guard_panel"
    seeds_helium_services = False

    def run(self, session: Session) -> CheckResult:
        profile = getattr(session, "profile_path", None)
        state_file = Path(profile) / STATE_FILE if profile else None

        def read() -> str | None:
            try:
                return state_file.read_text() if state_file else None
            except OSError:
                return None

        before = read()
        nav = session.navigate_url(PANEL_URL)
        if nav.get("error"):
            return CheckResult(self.name, "fail", f"cannot open {PANEL_URL}: {nav['error']}")
        obs = session.evaluate(IMPORT_PROBE, await_promise=True)
        time.sleep(1.0)
        if obs is not None:
            obs = {**obs, "state_before": before, "state_after": read(), "hosts": ["localhost", "127.0.0.1"]}
        problems = judge(obs)
        if problems:
            return CheckResult(self.name, "fail", "; ".join(problems))
        return CheckResult(self.name, "pass",
                           "chrome.send is undefined, the handler has no site without a captured tab, "
                           "and a switch call left state.json unchanged")
