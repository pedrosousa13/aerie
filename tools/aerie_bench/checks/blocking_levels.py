"""Blocking levels (issue #109): Standard lets the page's own site through built-in lists (custom lists still judge it), Aggressive blocks it.

A second browser starts on a profile whose ``AerieFilterLists`` directory is seeded with what the Settings page and the
Guard panel save: the default level ``standard`` and a per-site level ``aggressive`` for ``aggressive.localhost``
(``state.json``), plus two lists cached on disk (``lists/<id>.txt``; no address is fetched): a custom list with the rule
``/blocking/levels/important_custom.js$important``, and a built-in list (EasyPrivacy's slot, its text replaced by one hand-written
rule) with ``/blocking/levels/important_builtin.js$important``. The fixture page ``fixtures/blocking/levels.html`` asks for four
scripts, each from its own host (first party) and, except the content file, from ``127.0.0.1`` on the same port (third party):

* ``ad_header.js``: EasyList's generic rule ``/ad_header.js$script`` matches it;
* ``important_builtin.js``: the built-in list's ``$important`` rule matches it;
* ``important_custom.js``: the custom list's ``$important`` rule matches it;
* ``content.js``: nothing matches it; it must always load.

The page is opened as two sites:

* ``standard.localhost`` (the default level): the first-party EasyList and built-in ``$important`` requests are NOT blocked
  (the fixture server answers 404), the first-party custom-list ``$important`` request IS blocked (brave-core's second engine
  judges custom lists even first party), and all third-party requests are blocked (DevTools ``loadingFailed`` with
  ``net::ERR_BLOCKED_BY_CLIENT``);
* ``aggressive.localhost`` (its own level): all six requests are blocked. This is the control: it proves the rules match
  on this page, so a first-party request that loads on ``standard.localhost`` loads because of the level.

An engine that has no levels would block the first-party requests on ``standard.localhost``, so ``judge`` would fail with
"BLOCKED IN STANDARD". That was not run against such an engine; the build train does it. ``tests/test_bench_blocking_levels.py``
runs ``judge`` on hand-written observations.

What Brave does, from brave-core's ``AdBlockEngineWrapper::ShouldStartRequest``: for a first-party request outside Aggressive the
default engine's verdict is dropped, ``$important`` included; a third-party request keeps it; a second engine (custom lists and
subscriptions) still judges the first-party request. Aerie compiles the custom lists a second time, alone, for that. Brave's forced
Aggressive on youtube.com is not copied.
"""

from __future__ import annotations

import hashlib
import json
import time
from pathlib import Path

from ..session import CheckResult, Session
from . import register
from .ad_blocking import collect_outcomes

PAGE = "/blocking/levels.html"
STANDARD_HOST = "standard.localhost"
AGGRESSIVE_HOST = "aggressive.localhost"
THIRD_PARTY_HOST = "127.0.0.1"
BASE = "/blocking/levels/"
AD = "ad_header.js"
IMPORTANT_BUILTIN = "important_builtin.js"
IMPORTANT_CUSTOM = "important_custom.js"
CONTENT = "content.js"
LIST_URL = "https://bench-lists.example/levels.txt"  # Never fetched: the cached copy below is in use.
LIST_TEXT = f"! Title: Blocking levels fixture\n{BASE}{IMPORTANT_CUSTOM}$important\n"
BUILTIN_ID = "easyprivacy"  # Its cached copy is replaced by BUILTIN_TEXT, hand-written for this check.
BUILTIN_TEXT = f"! Title: Blocking levels built-in fixture\n{BASE}{IMPORTANT_BUILTIN}$important\n"
STATE_DIR = Path("AerieFilterLists")
STATE_FILE = STATE_DIR / "state.json"
SETTLE_SECONDS = 20.0
EVENT_METHODS = ("Network.requestWillBeSent", "Network.responseReceived", "Network.loadingFailed")


def list_id(url: str = LIST_URL) -> str:
    """The id the browser gives a custom list: "c" and the first 8 bytes of the SHA-256 of the address, in hex."""
    return "c" + hashlib.sha256(url.encode("ascii")).digest()[:8].hex()


def seeded_state(now_ms: int) -> dict:
    return {
        "auto_update": False,
        "last_update_check": str(now_ms),
        "default_level": "standard",
        "levels": {AGGRESSIVE_HOST: "aggressive"},
        "lists": [{"id": list_id(), "url": LIST_URL, "title": "bench", "builtin": False, "rules": 1,
                   "updated": str(now_ms), "error": ""},
                  {"id": BUILTIN_ID, "builtin": True, "rules": 1, "updated": str(now_ms), "error": ""}],
    }


def seed_profile(profile_path: Path, now_ms: int | None = None) -> Path:
    """Write the state and the cached list. Returns the state file's path."""
    now_ms = int(time.time() * 1000) if now_ms is None else now_ms
    root = Path(profile_path) / STATE_DIR
    (root / "lists").mkdir(parents=True, exist_ok=True)
    (root / "lists" / f"{list_id()}.txt").write_text(LIST_TEXT)
    (root / "lists" / f"{BUILTIN_ID}.txt").write_text(BUILTIN_TEXT)
    path = Path(profile_path) / STATE_FILE
    path.write_text(json.dumps(seeded_state(now_ms)))
    return path


def request_urls(host: str, port: int) -> dict[str, str]:
    """The request URLs of one visit, by name."""
    first = f"http://{host}:{port}{BASE}"
    third = f"http://{THIRD_PARTY_HOST}:{port}{BASE}"
    return {"ad_first": first + AD, "ad_third": third + AD,
            "builtin_first": first + IMPORTANT_BUILTIN, "builtin_third": third + IMPORTANT_BUILTIN,
            "custom_first": first + IMPORTANT_CUSTOM, "custom_third": third + IMPORTANT_CUSTOM,
            "content_first": first + CONTENT}


# What a visit must show, per level: True means the request must be blocked.
EXPECTED = {
    "standard": {"ad_first": False, "ad_third": True, "builtin_first": False, "builtin_third": True,
                 "custom_first": True, "custom_third": True, "content_first": False},
    "aggressive": {"ad_first": True, "ad_third": True, "builtin_first": True, "builtin_third": True,
                   "custom_first": True, "custom_third": True, "content_first": False},
}


def judge_visit(host: str, level: str, visit: dict | None, problems: list[str]) -> None:
    """``visit`` maps a request name to its outcome (``state``: blocked, loaded, failed or pending)."""
    if not visit:
        problems.append(f"{host}: the page was not observed")
        return
    for name, must_block in EXPECTED[level].items():
        state = (visit.get(name) or {}).get("state")
        if state is None or state == "pending":
            problems.append(f"{host}: {name} was never answered")
        elif must_block and state != "blocked":
            problems.append(f"{host}: {name} was NOT BLOCKED in {level} (outcome {state}), but must be")
        elif not must_block and state == "blocked":
            what = "BLOCKED IN STANDARD" if level == "standard" else "BLOCKED"
            problems.append(f"{host}: {name} was {what} (first-party content must load, and Standard lets the page's "
                            "own site through built-in lists, but not a custom list's rules)")
        else:
            status = (visit.get(name) or {}).get("status")
            wanted = (200,) if name == "content_first" else (200, 404)
            if not must_block and status not in wanted:
                problems.append(f"{host}: {name} did not reach the fixture server (status {status})")


def judge(obs: dict | None) -> tuple[str, str, dict]:
    """``obs``: ``seeded`` (the profile was written), ``standard`` and ``aggressive`` (visits)."""
    if not obs:
        return "fail", "no observations: the second browser did not run", {}
    problems: list[str] = []
    if not obs.get("seeded"):
        problems.append("the profile was not seeded")
    judge_visit(STANDARD_HOST, "standard", obs.get("standard"), problems)
    judge_visit(AGGRESSIVE_HOST, "aggressive", obs.get("aggressive"), problems)
    data = {k: obs.get(k) for k in ("seeded", "standard", "aggressive")}
    if problems:
        return "fail", f"{len(problems)} problem(s): " + "; ".join(problems), data
    return "pass", (f"{STANDARD_HOST} (default level Standard): its own EasyList and built-in $important requests "
                    f"loaded, its custom-list $important request and every third-party one were blocked; {AGGRESSIVE_HOST} "
                    "(level Aggressive): all six were blocked"), data


@register
class BlockingLevels:
    name = "blocking_levels"
    seeds_helium_services = False

    def __init__(self, settle_seconds: float = SETTLE_SECONDS):
        self.settle_seconds = settle_seconds

    def _visit(self, session: Session, host: str) -> dict:
        session.cdp("Network.enable")
        for method in EVENT_METHODS:  # Drop what the last page left.
            session.take_events(method)
        nav = session.navigate_url(f"http://{host}:{session.fixtures.port}{PAGE}")
        if nav["error"]:
            return {}
        deadline = time.monotonic() + self.settle_seconds
        while time.monotonic() < deadline and session.evaluate("window.__levelsDone === true") is not True:
            time.sleep(0.2)
        events: list[dict] = []
        for method in EVENT_METHODS:
            events += session.take_events(method)
        order = {m: i for i, m in enumerate(EVENT_METHODS)}
        outcomes = collect_outcomes(sorted(events, key=lambda e: order.get(e.get("method"), 3)))
        urls = request_urls(host, session.fixtures.port)
        return {name: outcomes.get(url, {}) for name, url in urls.items()}

    def run(self, session: Session) -> CheckResult:
        with session.new_profile() as profile:
            seeded = seed_profile(profile.path).is_file()
            with session.variant(profile=profile) as browser:
                standard = self._visit(browser, STANDARD_HOST)
                aggressive = self._visit(browser, AGGRESSIVE_HOST)
                browser.screenshot("blocking_levels.png")
        status, detail, data = judge({"seeded": seeded, "standard": standard, "aggressive": aggressive})
        return CheckResult(self.name, status, detail, data=data)
