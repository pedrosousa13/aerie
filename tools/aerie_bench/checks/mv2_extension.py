"""Manifest V2 still works: a tiny MV2 extension installs, runs, and blocks a request, and uBlock Origin is gone.

Issue #8 removes Helium's bundled uBlock Origin but keeps Manifest V2 support, so
that users can install uBlock Origin (or any MV2 extension) themselves. This check
proves the second half on a real build:

* the fixture extension (``extensions/mv2``, ``manifest_version: 2``) is installed
  with the DevTools command ``Extensions.loadUnpacked``. The Bench's DevTools pipe is a
  browser-target session, which is what allows that command (no launch flag);
* its persistent background page started (it is a DevTools target);
* its content script ran on the fixture page (``data-aerie-mv2`` on the page);
* its blocking ``webRequest`` listener cancelled ``/mv2/ext-block-me.js`` before it
  went out (``ERR_BLOCKED_BY_CLIENT``) and left ``/mv2/allowed.js`` alone. The extension's
  background page keeps a list of what it cancelled (``aerieCancelled``); the check reads
  it from the background target and requires the URL in it, so it was the extension, not
  the Aerie engine, that cancelled the request;
* no target of the bundled uBlock Origin component exists.

Neither fixture URL is on any filter list (checked against EasyList and
EasyPrivacy), so the Aerie engine does not decide the outcome.
"""

from __future__ import annotations

import json
import time
from pathlib import Path

from ..cdp import CdpError
from ..session import CheckResult, Session
from . import register
from .ad_blocking import BLOCKED_ERROR, collect_outcomes

EXTENSION_DIR = Path(__file__).resolve().parents[1] / "extensions" / "mv2"
PAGE = "/mv2/index.html"
BLOCKED_PATH = "/mv2/ext-block-me.js"
ALLOWED_PATH = "/mv2/allowed.js"
# The id Helium's bundled uBlock Origin ran under as a component extension.
UBO_COMPONENT_ID = "blockjmkbacgjkknlgpkjjiijinjdanf"
SETTLE_SECONDS = 10.0
# The extension's background page registers its listener some time after the extension
# loads, and the browser learns of it later still. The check fetches this URL from the
# background page until the listener cancels it, and only then loads the fixture page.
PROBE_QUERY = "aerie-probe"
PROBE_SECONDS = 10.0


def collect(events: list[dict]) -> dict[str, dict]:
    return collect_outcomes(events)


def _outcome(outcomes: dict[str, dict], path: str) -> dict | None:
    return next((o for url, o in outcomes.items() if url.split("?", 1)[0].endswith(path)), None)


def judge(load: dict | None, load_error: str | None, marker, allowed_ran, outcomes: dict[str, dict],
          targets: list[dict], extension_cancels: list[str] | None, probe: dict | None = None) -> tuple[str, str, dict]:
    problems: list[str] = []
    ext_id = (load or {}).get("id")
    if not ext_id:
        problems.append(f"the MV2 extension did not install: {load_error or 'no extension id returned'}")
    else:
        prefix = f"chrome-extension://{ext_id}/"
        if not any(t.get("url", "").startswith(prefix) and t.get("type") in ("background_page", "service_worker")
                   for t in targets):
            problems.append("the extension's persistent background page did not start (no background target)")
        if marker != "ran":
            problems.append("the extension's content script did not run on the fixture page")
    blocked = _outcome(outcomes, BLOCKED_PATH)
    page_cancels = [url for url in (extension_cancels or []) if PROBE_QUERY not in url]
    if ext_id and probe is not None and not probe.get("ready"):
        problems.append(f"the extension's blocking webRequest listener never took effect: a fetch from its own "
                        f"background page was not cancelled after {probe.get('attempts')} attempt(s) in "
                        f"{probe.get('seconds')}s, so this is not about the page's request or the proxy order")
    if ext_id:
        if blocked is None:
            problems.append(f"the page never requested {BLOCKED_PATH}")
        elif blocked["state"] != "blocked":
            problems.append(f"the extension's blocking webRequest did not block {BLOCKED_PATH} "
                            f"({blocked['state']}, {blocked['error'] or blocked['status']}; want {BLOCKED_ERROR})")
        if not any(url.split("?", 1)[0].endswith(BLOCKED_PATH) for url in page_cancels):
            problems.append(f"the extension did not record cancelling {BLOCKED_PATH} (cancelled: "
                            f"{extension_cancels if extension_cancels is not None else 'unreadable'}): "
                            "the block was not the extension's")
        allowed = _outcome(outcomes, ALLOWED_PATH)
        if allowed is None or allowed["state"] != "loaded" or allowed["status"] != 200 or not allowed_ran:
            problems.append(f"the allowed script {ALLOWED_PATH} did not load and run")
    if any(UBO_COMPONENT_ID in t.get("url", "") for t in targets):
        problems.append("the bundled uBlock Origin component is still running "
                        f"(a target under chrome-extension://{UBO_COMPONENT_ID}/)")
    data = {"extension_id": ext_id, "marker": marker, "allowed_ran": allowed_ran,
            "blocked": blocked and {"state": blocked["state"], "error": blocked["error"]},
            "extension_cancels": extension_cancels, "listener_probe": probe, "extension_targets": sorted(t["url"] for t in targets if t.get("url", "").startswith("chrome-extension://"))}
    if problems:
        return "fail", "; ".join(problems), data
    return "pass", f"MV2 extension {ext_id} installed, ran, blocked a request and left another; no uBlock Origin component", data


@register
class Mv2Extension:
    name = "mv2_extension"
    seeds_helium_services = False

    def __init__(self, settle_seconds: float = SETTLE_SECONDS, probe_seconds: float = PROBE_SECONDS):
        self.settle_seconds = settle_seconds
        self.probe_seconds = probe_seconds

    @staticmethod
    def _background(session: Session, load: dict | None, targets: list[dict]) -> dict | None:
        ext_id = (load or {}).get("id")
        return next((t for t in targets if ext_id and t.get("url", "").startswith(f"chrome-extension://{ext_id}/")
                     and t.get("type") in ("background_page", "service_worker")), None)

    @staticmethod
    def _targets(session: Session) -> list[dict]:
        try:
            return session.client.send("Target.getTargets", timeout=session.timeout).get("targetInfos", [])
        except CdpError:
            return []

    @staticmethod
    def _attach(session: Session, background: dict | None) -> str | None:
        if not background:
            return None
        try:
            return session.client.send("Target.attachToTarget", {"targetId": background["targetId"], "flatten": True},
                                       timeout=session.timeout)["sessionId"]
        except (CdpError, KeyError):
            return None

    def _wait_for_listener(self, session: Session, load: dict | None) -> tuple[str | None, dict]:
        """Wait until the extension's listener is live in the browser. Returns the background session and a report."""
        start = time.monotonic()
        deadline = start + self.probe_seconds
        url = f"{session.fixtures.base_url}{BLOCKED_PATH}?{PROBE_QUERY}"
        sid, attempts, ready = None, 0, False
        while True:
            if sid is None:
                sid = self._attach(session, self._background(session, load, self._targets(session)))
            if sid is not None:
                attempts += 1
                try:
                    answer = session.client.send(
                        "Runtime.evaluate", {"expression": f"self.aerieProbe({json.dumps(url)})", "returnByValue": True,
                                             "awaitPromise": True}, session_id=sid, timeout=session.timeout)
                    ready = answer.get("result", {}).get("value") is True
                except (CdpError, KeyError, ValueError, TypeError):
                    ready = False
            if ready or time.monotonic() > deadline:
                break
            time.sleep(0.1)
        return sid, {"ready": ready, "attempts": attempts, "seconds": round(time.monotonic() - start, 2)}

    @staticmethod
    def _extension_cancels(session: Session, sid: str | None) -> list[str] | None:
        """What the extension's background page says it cancelled, or None if it cannot be read."""
        if sid is None:
            return None
        try:
            answer = session.client.send(
                "Runtime.evaluate", {"expression": "JSON.stringify(self.aerieCancelled || [])", "returnByValue": True},
                session_id=sid, timeout=session.timeout)
            return json.loads(answer["result"]["value"])
        except (CdpError, KeyError, ValueError, TypeError):
            return None

    def run(self, session: Session) -> CheckResult:
        load, load_error = None, None
        try:
            load = session.client.send("Extensions.loadUnpacked", {"path": str(EXTENSION_DIR)}, timeout=session.timeout)
        except CdpError as exc:
            load_error = str(exc)
        # Load the page only once the listener is live, or the page can win the race (issue #40).
        background_sid, probe = self._wait_for_listener(session, load) if (load or {}).get("id") else (None, None)
        session.cdp("Network.enable")
        session.navigate(PAGE)
        events: list[dict] = []
        deadline = time.monotonic() + self.settle_seconds
        while True:
            for method in ("Network.requestWillBeSent", "Network.responseReceived", "Network.loadingFailed"):
                events += session.take_events(method)
            outcomes = collect(events)
            done = all((o := _outcome(outcomes, p)) and o["state"] != "pending" for p in (BLOCKED_PATH, ALLOWED_PATH))
            if done or time.monotonic() > deadline:
                break
            time.sleep(0.2)
        marker = session.evaluate("document.documentElement.getAttribute('data-aerie-mv2')")
        allowed_ran = bool(session.evaluate("window.mv2AllowedRan === true"))
        targets = self._targets(session)
        cancels = self._extension_cancels(session, background_sid)
        status, detail, data = judge(load, load_error, marker, allowed_ran, outcomes, targets, cancels, probe)
        return CheckResult(self.name, status, detail, data=data)
