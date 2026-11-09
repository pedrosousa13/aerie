"""Aerie Guard panel (issue #100): the page with no captured tab, and the bubble opened again.

Part one loads the page straight into a tab. There is no bubble there, so the handler has no target.
It proves, on the real browser:

1. ``chrome.send`` is undefined on ``chrome://aerie-guard.top-chrome``.
2. The page's own Mojo handler answers ``getState`` with null (no site).
3. A ``setGuardOn(false)`` call through that handler changes no exception in
   ``AerieFilterLists/state.json`` and no exception appears for the host of any open tab.

Part two uses the bubble, in a second browser started with UI DevTools (``ui_devtools.FLAGS``), which
presses the Guard icon in the address bar as a user does. It opens the panel, closes it with Escape,
navigates the tab to another page, opens the panel again, closes it, and then proves the browser is
still alive: its page answers and it exits cleanly. The second open used to crash the whole browser.

``judge`` and ``judge_reopen`` are plain logic that the tests run on recorded observations: a handler that
takes a host from the page, and a browser that dies on the second open (the negative controls).
"""

from __future__ import annotations

import json
import time
from pathlib import Path

from .. import ui_devtools
from ..cdp import CdpError
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


# Part two, in order. Each is True once it happened; the run stops at the first that did not.
REOPEN_STEPS = ("opened", "closed", "navigated", "reopened", "closed_again", "alive")
REOPEN_WHAT = {
    "opened": "pressing the Guard icon did not open the panel",
    "closed": "Escape did not close the panel",
    "navigated": "the tab did not navigate after the panel closed",
    "reopened": "pressing the Guard icon again, after the panel closed and the tab navigated, did not open it",
    "closed_again": "Escape did not close the reopened panel",
    "alive": "the browser did not answer after the panel opened a second time",
}
GUARD_ICON_CLASS = "PageActionView"
GUARD_TOOLTIP_PREFIX = "Aerie Guard"  # IDS_AERIE_GUARD_TOOLTIP_*: every state starts so


def judge_reopen(obs: dict | None) -> list[str]:
    """Return the problems. ``obs``: one bool per REOPEN_STEPS, ``error`` (what stopped the run) and
    ``exit`` (the second browser's exit: ``clean``, ``code``)."""
    if not obs:
        return ["no observations: the second browser did not start"]
    problems: list[str] = []
    for step in REOPEN_STEPS:
        if not obs.get(step):
            problems.append(REOPEN_WHAT[step] + (f" ({obs['error']})" if obs.get("error") else ""))
            break
    exit_info = obs.get("exit") or {}
    code = exit_info.get("code")
    if isinstance(code, int) and code < 0:
        problems.append(f"the browser was killed by signal {-code} (it crashed)")
    elif not exit_info.get("clean"):
        problems.append(f"the browser did not exit cleanly ({exit_info or 'no exit status'})")
    return problems


def exceptions(state: str | None):
    """The ``exceptions`` of a state.json text (none for no file); the raw text if it is not JSON."""
    if state is None:
        return []
    try:
        return json.loads(state).get("exceptions", [])
    except (ValueError, AttributeError):
        return state


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
    # The exceptions are what a switch call writes. The rest of the file is the manager's own: a fresh
    # profile has no state.json until the manager first saves it, which can be during this check.
    if exceptions(after) != exceptions(before):
        problems.append("a switch call from the page changed the exceptions in state.json")
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
        reopen = self.reopen(session)
        problems += judge_reopen(reopen)
        data = {"direct": obs, "reopen": reopen}
        if problems:
            return CheckResult(self.name, "fail", "; ".join(problems), data=data)
        return CheckResult(self.name, "pass",
                           "chrome.send is undefined, the handler has no site without a captured tab, "
                           "a switch call left state.json unchanged, and the icon opened the panel again "
                           "after it closed and the tab navigated, with the browser alive", data=data)

    def reopen(self, session: Session) -> dict:
        """Part two: open, close, navigate, open again, close, in a browser of its own."""
        obs: dict = {step: False for step in REOPEN_STEPS}
        with session.new_profile() as profile, \
                session.variant(extra_flags=ui_devtools.FLAGS, profile=profile) as browser:
            ui = None
            try:
                browser.navigate("/browse-a.html")
                ui = ui_devtools.connect(profile.path)
                obs["opened"] = self._press(browser, ui) is not None
                if obs["opened"]:
                    obs["closed"] = self._escape(browser)
                if obs["closed"]:
                    browser.navigate("/browse-b.html")
                    obs["navigated"] = browser.evaluate("location.pathname") == "/browse-b.html"
                if obs["navigated"]:
                    obs["reopened"] = self._press(browser, ui) is not None
                if obs["reopened"]:
                    obs["closed_again"] = self._escape(browser)
                if obs["closed_again"]:
                    obs["alive"] = browser.evaluate("1 + 1") == 2 and bool(browser.targets())
            except (CdpError, ui_devtools.UiDevToolsError, OSError) as exc:
                obs["error"] = f"{type(exc).__name__}: {exc}"
            finally:
                if ui is not None:
                    ui.close()
        obs["exit"] = browser.exit
        return obs

    @staticmethod
    def _panel(browser: Session, shown: bool, timeout: float = 10.0) -> dict | None:
        """Wait until a panel page is visible (``shown``), or none is (not ``shown``). Returns the
        visible one's target, or None."""
        deadline = time.monotonic() + timeout
        while True:
            # A closed panel's page can stay a while, hidden, next to a new one.
            target = next((t for t in browser.targets()
                           if t.get("type") == "browser_ui" and t.get("url", "").startswith(PANEL_URL)
                           and AerieGuardPanel._visibility(browser, t) == "visible"), None)
            if (target is not None) == shown:
                return target
            if time.monotonic() >= deadline:
                raise CdpError(f"the panel did not {'open' if shown else 'close'} within {timeout:.0f} s")
            time.sleep(0.25)

    @staticmethod
    def _visibility(browser: Session, target: dict) -> str | None:
        sid = browser.client.send("Target.attachToTarget", {"targetId": target["targetId"], "flatten": True},
                                  timeout=browser.timeout)["sessionId"]
        try:
            res = browser.client.send("Runtime.evaluate", {"expression": "document.visibilityState",
                                                           "returnByValue": True},
                                      session_id=sid, timeout=browser.timeout)
            return res.get("result", {}).get("value")
        finally:
            browser.client.send("Target.detachFromTarget", {"sessionId": sid}, timeout=browser.timeout)

    def _press(self, browser: Session, ui: ui_devtools.UiDevTools) -> dict | None:
        found = ui.find_view(GUARD_ICON_CLASS, GUARD_TOOLTIP_PREFIX)
        if found is None:
            raise CdpError("no Guard icon in the address bar")
        ui.press(*found)
        return self._panel(browser, shown=True)

    def _escape(self, browser: Session) -> bool:
        """Escape in the panel, as a user closes it. The panel's page may stay cached, hidden."""
        target = self._panel(browser, shown=True)
        sid = browser.client.send("Target.attachToTarget", {"targetId": target["targetId"], "flatten": True},
                                  timeout=browser.timeout)["sessionId"]
        for kind in ("rawKeyDown", "keyUp"):
            # A native key code lets the key reach the browser: the bubble, not the page, closes on it.
            browser.client.send("Input.dispatchKeyEvent", {
                "type": kind, "key": "Escape", "code": "Escape",
                "windowsVirtualKeyCode": 27, "nativeVirtualKeyCode": 27}, session_id=sid, timeout=browser.timeout)
        self._panel(browser, shown=False)
        return True
