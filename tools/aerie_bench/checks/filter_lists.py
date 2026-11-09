"""Filter lists: the Aerie Guard section of Settings manages lists and can turn blocking off per site.

The browser runs with ``--aerie-test-allow-loopback-filter-lists`` (http list URLs only on
loopback). The check drives the Settings message handler (``AerieGuardSettingsHandler``, issue #103) on
``chrome://settings/aerieGuard`` through ``sendWithPromise``: every ``aerieGuard.<name>`` call answers with
the state (``getState`` is the same answer without a change) or rejects its promise for an argument that
does not fit. The fixture server serves a list (``/filters/custom-list.txt``, one rule that blocks
``/filters/ads/``), a web page that is not a list (``/filters/not-a-list.html``) and a page
(``/filters/page.html``) that asks for three ads, a first-party script and a "trap" script that
only the HTML page's rule-like line would block. The page is opened as ``news.localhost`` and
``other.localhost``: two sites that both reach the loopback fixture server.

What it proves (the numbers are the issue #10 acceptance criteria, then the ones of issue #103):

1. Negative controls: an ``http://`` list URL on a non-loopback host and a list that is an HTML page
   both end up with an error in the state, and the HTML page blocks nothing.
2. Adding the fixture list URL takes effect with no restart: the list loads with rules and no
   error, and the next page visit has its three ads blocked while first-party content loads.
3. The blocked count: ``Aerie.Adblock.PageBlockedCount`` has one sample, the number of blocked
   fixture ads, after the tab left the page.
4. A forced update (``aerieGuard.updateAll``; the real one runs 24 hours after install) fetches the
   custom list again, and every non-loopback request in the NetLog is to a list publisher's own
   https URL (the built-in lists' own URLs fail to resolve in the Bench, which is fine). No other
   host, and no http request, may appear.
5. An exception for ``news.localhost`` lets the ads load there, and they stay blocked on
   ``other.localhost``.
6. Validation (issue #103): a non-string host, a 5000-character host, a 5000-character list address, a
   non-string list id and a non-boolean switch are each refused (the promise rejects) and
   ``AerieFilterLists/state.json`` is byte for byte the same afterwards. A handler without the length
   check accepts the long host, so this step fails against it: see ``judge_validation``.
7. The browser restarts on the same profile (``session.new_profile`` and ``session.variant(profile=)``)
   and the list and the exception are still in effect. This is a separate, tested half: see
   ``judge_restart`` and ``FilterLists.run_second_launch``.

The incognito step of the old ``chrome://filters`` page is not here: Settings always opens in the regular
profile and never shows or sets an incognito exception, and ``chrome://settings`` must not be opened in a
CDP incognito context (it crashes). The session rules (an incognito exception stays in memory, cannot undo a
saved one) stay covered by the manager's unit tests.

Decision logic is in plain functions (``judge``, ``judge_restart``, ``judge_validation``, ...) that the tests
run on recorded observations; ``observe_first_launch`` and ``observe_second_launch`` do the browser driving.
"""

from __future__ import annotations

import json
import time
from pathlib import Path
from urllib.parse import urlsplit

from .. import netlog
from ..session import CheckResult, Session
from . import register
from .ad_blocking import boolean_counts

FLAG = "--aerie-test-allow-loopback-filter-lists"
MANAGER_URL = "chrome://settings/aerieGuard"
PAGE_PATH = "/filters/page.html"
LIST_PATH = "/filters/custom-list.txt"
HTML_LIST_PATH = "/filters/not-a-list.html"
INSECURE_LIST_URL = "http://lists.bench.example/insecure-list.txt"
EXCEPTED_HOST = "news.localhost"
OTHER_HOST = "other.localhost"
LONG_ARG = "a" * 5000  # over the handler's limit of 4096
STATE_FILE = "AerieFilterLists/state.json"  # inside the profile
N_ADS = 3
COUNT_HISTOGRAM = "Aerie.Adblock.PageBlockedCount"
UPDATE_HISTOGRAM = "Aerie.Adblock.ListUpdateSuccess"  # recorded in the report, no threshold
BLOCKED_HISTOGRAM = "Aerie.Adblock.RequestBlocked"
# Publishers of the lists the engine ships; the Bench cannot reach them (the update fails, tolerated).
PUBLISHER_HOSTS = ("easylist.to", "www.easylist.to")
STATE_TIMEOUT = 30.0
RECORDING_NAME = "filter_lists-recording.json"
SETTLE = 1.5  # seconds for the state file write that follows a change


# ---------------------------------------------------------------- pure decision logic

def histogram_samples(hist: dict) -> dict[int, int]:
    """{value: count} from a DevTools histogram of an exact-linear metric (bucket low = the value)."""
    out: dict[int, int] = {}
    for bucket in hist.get("buckets", []):
        if bucket.get("count", 0):
            out[bucket["low"]] = out.get(bucket["low"], 0) + bucket["count"]
    return out


def find_list(state: dict | None, url: str) -> dict | None:
    return next((x for x in (state or {}).get("lists", []) if x.get("url") == url), None)


def judge_visit(visit: dict | None, label: str, ads_blocked: bool, problems: list[str]) -> None:
    """A page visit: three ads blocked (or loaded), first-party content and the trap script loaded.

    DevTools-side result (the page's onload/onerror) and the fixture server's log must agree."""
    if not visit or not visit.get("done"):
        problems.append(f"{label}: the page did not finish loading its scripts")
        return
    ads = visit.get("ads", [])
    seen = visit.get("fixture_ad_requests", 0)
    if ads_blocked:
        if ads != [False] * N_ADS:
            problems.append(f"{label}: expected {N_ADS} blocked ads, the page says {ads}")
        if seen:
            problems.append(f"{label}: blocked ads still reached the fixture server ({seen} request(s))")
    else:
        if ads != [True] * N_ADS:
            problems.append(f"{label}: expected {N_ADS} ads to load, the page says {ads}")
        if seen != N_ADS:
            problems.append(f"{label}: the fixture server saw {seen} ad request(s), expected {N_ADS}")
    if visit.get("first_party") is not True:
        problems.append(f"{label}: first-party content did not load")
    if visit.get("trap") is not True:
        problems.append(f"{label}: the trap script was blocked, so a list that should not apply did apply")


def judge_control(name: str, entry: dict | None, problems: list[str]) -> None:
    if entry is None:
        problems.append(f"negative control {name}: the list is not in the state at all, so no error is shown")
    elif not entry.get("error"):
        problems.append(f"negative control {name}: accepted without an error: {entry}")


def judge_page_count(samples: dict[int, int] | None, expected: int, problems: list[str]) -> None:
    """Exactly one non-zero sample, equal to the blocked fixture requests. Zero samples (the
    manager page or about:blank being left) carry no ads and are tolerated."""
    if samples is None:
        problems.append(f"no {COUNT_HISTOGRAM} histogram: the browser does not count blocked requests per page")
        return
    counted = {v: n for v, n in samples.items() if v != 0}
    if counted != {expected: 1}:
        problems.append(f"{COUNT_HISTOGRAM} samples {dict(sorted(samples.items()))}, expected one sample of {expected}")


def update_requests(records: list[dict], state_lists: list[dict]) -> tuple[list[str], list[str]]:
    """(urls of publisher requests, urls of requests that no list publisher owns), from the NetLog.

    Loopback URLs are the fixture server and are left out. A non-loopback URL is legitimate only if
    it is https and either a built-in publisher's host or the URL of a list in the state."""
    list_urls = {x["url"] for x in state_lists}
    ok, bad = [], []
    for url in sorted(netlog.all_urls(records)):
        parts = urlsplit(url)
        if parts.scheme not in ("http", "https", "ws", "wss", "ftp") or netlog.is_loopback(parts.hostname or ""):
            continue
        legit = parts.scheme == "https" and ((parts.hostname or "") in PUBLISHER_HOSTS or url in list_urls)
        (ok if legit else bad).append(url)
    return ok, bad


def judge_update(update: dict | None, problems: list[str]) -> dict:
    """The forced update: the custom list was fetched again, nothing else was contacted."""
    if not update:
        problems.append("the forced update was not observed")
        return {}
    if update["hits_after"] <= update["hits_before"]:
        problems.append(f"the update did not fetch the custom list again ({update['hits_before']} -> {update['hits_after']} fixture hits)")
    custom = find_list(update.get("state"), update["list_url"])
    if custom is None or custom.get("error"):
        problems.append(f"the custom list has an error after the update: {custom}")
    for entry in (update.get("state") or {}).get("lists", []):
        if not entry.get("builtin") and entry.get("error"):
            problems.append(f"a user list failed to update: {entry['url']} ({entry['error']})")
    publisher, foreign = update_requests(update.get("netlog_records", []), (update.get("state") or {}).get("lists", []))
    if foreign:
        problems.append("requests that no list publisher owns: " + ", ".join(foreign))
    return {"publisher_requests": publisher, "foreign_requests": foreign,
            "builtin_update_errors": [x["url"] for x in (update.get("state") or {}).get("lists", [])
                                      if x.get("builtin") and x.get("error")]}


def judge_restart(restart: dict | None, list_url: str = "", excepted_host: str = EXCEPTED_HOST) -> list[str]:
    """The half after a browser restart on the same profile. Returns the problems."""
    problems: list[str] = []
    if not restart:
        return ["the browser was not restarted on the same profile"]
    state = restart.get("state") or {}
    if excepted_host not in state.get("exceptions", []):
        problems.append(f"after restart the exception for {excepted_host} is gone ({state.get('exceptions')})")
    if list_url:
        entry = find_list(state, list_url)
        if entry is None:
            problems.append("after restart the custom list is gone")
        elif entry.get("error"):
            problems.append(f"after restart the custom list has an error: {entry['error']}")
    judge_visit(restart.get("excepted_visit"), f"after restart, {excepted_host}", False, problems)
    judge_visit(restart.get("other_visit"), f"after restart, {OTHER_HOST}", True, problems)
    return problems


def judge_validation(val: dict | None) -> list[str]:
    """The validation step: every probe was refused and the state file did not change.

    ``val["probes"]`` is a list of ``{"name", "refused"}`` (``refused`` is true when the promise rejected);
    ``val["state_before"]`` and ``val["state_after"]`` are the text of ``state.json`` around the probes (None
    when it could not be read). A handler that answers a bad argument with a state, such as one without the
    length check, is not refused."""
    if not val or not val.get("probes"):
        return ["no validation observations: the check never sent a bad argument"]
    problems: list[str] = []
    for probe in val["probes"]:
        if probe.get("refused") is not True:
            problems.append(f"validation: {probe.get('name')} was not refused")
    before, after = val.get("state_before"), val.get("state_after")
    if before is None or after is None:
        problems.append("validation: the state file could not be read, so it is not proven that nothing changed")
    elif before != after:
        problems.append("validation: state.json changed while only bad arguments were sent")
    return problems


def judge(obs: dict) -> tuple[str, str, dict]:
    """Return (status, detail, data) for the observations of both launches."""
    problems: list[str] = []
    judge_control("insecure http list", obs.get("insecure_entry"), problems)
    judge_control("html page as list", obs.get("html_entry"), problems)
    for name in ("insecure_entry", "html_entry"):
        if obs.get(f"{name}_removed") is False:
            problems.append(f"negative control {name}: removeList left it in the state")
    added = obs.get("added_entry")
    if added is None or added.get("error") or not added.get("rules"):
        problems.append(f"adding the custom list did not give a loaded list ({added})")
    judge_visit(obs.get("blocked_visit"), "after adding the list (no restart)", True, problems)
    judge_page_count(obs.get("page_count_samples"), N_ADS, problems)
    engine = obs.get("request_blocked")
    if engine is None or engine["true"] < N_ADS:
        problems.append(f"{BLOCKED_HISTOGRAM} has {engine and engine['true']} true sample(s), need at least {N_ADS}")
    update_data = judge_update(obs.get("update"), problems)
    exc = obs.get("exception") or {}
    if EXCEPTED_HOST not in (exc.get("state") or {}).get("exceptions", []):
        problems.append(f"setException did not put {EXCEPTED_HOST} in the state")
    judge_visit(exc.get("excepted_visit"), f"exception, {EXCEPTED_HOST}", False, problems)
    judge_visit(exc.get("other_visit"), f"exception, {OTHER_HOST}", True, problems)
    problems += judge_validation(obs.get("validation"))
    problems += judge_restart(obs.get("restart"), obs.get("list_url", ""))
    data = {"update": update_data, "page_count_samples": obs.get("page_count_samples"),
            "request_blocked": engine, "restart": (obs.get("restart") or {}).get("state")}
    if problems:
        return "fail", f"{len(problems)} problem(s): " + "; ".join(problems), data
    return "pass", (f"custom list took effect without restart, {N_ADS} ads blocked and counted, update contacted only "
                    f"list publishers ({len(update_data['publisher_requests'])} request(s)), exception for "
                    f"{EXCEPTED_HOST} only and it survived a restart; {len(obs['validation']['probes'])} bad arguments were refused "
                    f"and left state.json unchanged; both negative controls rejected"), data


# ---------------------------------------------------------------- browser driving

CALL_JS = """(async () => {
  const {sendWithPromise} = await import('chrome://resources/js/cr.js');
  try { return {ok: true, state: await sendWithPromise(%s, ...%s)}; }
  catch (e) { return {ok: false, error: String(e)}; }
})()"""


def call(session: Session, name: str, args: list | None = None) -> dict:
    """One ``aerieGuard.<name>`` message through ``sendWithPromise``. Returns ``{"ok": True, "state": ...}`` or,
    when the handler refused the arguments, ``{"ok": False, "error": ...}``."""
    return session.evaluate(CALL_JS % (json.dumps("aerieGuard." + name), json.dumps(args or [])), await_promise=True)


def send(session: Session, name: str, args: list | None = None) -> dict | None:
    """``call`` for a change: the state it answers with, or None if it was refused."""
    result = call(session, name, args)
    return result["state"] if result.get("ok") else None


def read_state(session: Session) -> dict | None:
    return send(session, "getState")


def open_manager(session: Session, timeout: float = STATE_TIMEOUT) -> dict | None:
    nav = session.navigate_url(MANAGER_URL)
    if nav["error"]:
        raise RuntimeError(f"cannot open {MANAGER_URL}: {nav['error']}")
    session.wait_for("typeof chrome !== 'undefined' && typeof chrome.send === 'function'", timeout=timeout)
    return read_state(session)


def wait_state(session: Session, accept, timeout: float = STATE_TIMEOUT) -> dict | None:
    """Poll the handler's state until ``accept(state)``; return the last state either way."""
    deadline = time.monotonic() + timeout
    while True:
        state = read_state(session)
        if state and accept(state):
            return state
        if time.monotonic() >= deadline:
            return state
        time.sleep(0.25)


def add_list(session: Session, url: str) -> dict | None:
    """Add a list and wait until it has either loaded, got an error, or been refused. Returns its
    entry. A list the manager refuses on the spot (a malformed or insecure address) never gets an
    entry: the answer's ``addError`` says why, and that is returned as the entry's error."""
    answer = send(session, "addList", [url])
    if answer and find_list(answer, url) is None and answer.get("addError"):
        return {"url": url, "error": answer["addError"], "refused": True}
    state = wait_state(session, lambda s: (e := find_list(s, url)) is not None and (e.get("error") or e.get("updated")))
    return find_list(state, url)


def remove_list(session: Session, url: str) -> bool:
    entry = find_list(read_state(session), url)
    if entry is None:
        return True
    send(session, "removeList", [entry["id"]])
    state = wait_state(session, lambda s: find_list(s, url) is None)
    return find_list(state, url) is None


def visit_page(session: Session, host: str, visit_id: str) -> dict:
    """Open the fixture page as ``host`` and return what it saw, plus the ad requests the fixture server got."""
    port = session.fixtures.port
    session.navigate_url(f"http://{host}:{port}{PAGE_PATH}?visit={visit_id}")
    session.wait_for("window.__filterResults && window.__filterResults.done === true", timeout=15.0)
    result = session.evaluate("window.__filterResults") or {}
    marker = f"visit={visit_id}"
    result["fixture_ad_requests"] = sum(
        1 for r in session.fixtures.snapshot()
        if r["path"].startswith("/filters/ads/") and marker in r["path"] and r["status"] == 200)
    return result


def read_page_count(session: Session) -> dict[int, int] | None:
    session.navigate_url("about:blank")  # leaving the page records its blocked count
    time.sleep(1.0)
    try:
        hist = session.client.send("Browser.getHistogram", {"name": COUNT_HISTOGRAM}, timeout=session.timeout)["histogram"]
    except Exception:
        return None
    return histogram_samples(hist)


def read_boolean(session: Session, name: str) -> dict | None:
    try:
        return boolean_counts(session.client.send("Browser.getHistogram", {"name": name},
                                                  timeout=session.timeout)["histogram"])
    except Exception:
        return None


def list_url_of(session: Session, path: str) -> str:
    return f"{session.fixtures.base_url}{path}"


def custom_hits(session: Session) -> int:
    return sum(1 for r in session.fixtures.snapshot() if r["path"].split("?", 1)[0] == LIST_PATH)


def observe_first_launch(session: Session, profile_path: str | Path | None = None) -> dict:
    """Controls, add, blocked visit, count, update and exception, on a live browser. Returns observations
    (everything but the NetLog, which only exists after exit)."""
    custom_url, html_url = list_url_of(session, LIST_PATH), list_url_of(session, HTML_LIST_PATH)
    obs: dict = {"list_url": custom_url}
    obs["initial_state"] = open_manager(session)
    obs["insecure_entry"] = add_list(session, INSECURE_LIST_URL)
    obs["html_entry"] = add_list(session, html_url)
    obs["insecure_entry_removed"] = remove_list(session, INSECURE_LIST_URL)
    obs["html_entry_removed"] = remove_list(session, html_url)
    obs["added_entry"] = add_list(session, custom_url)
    obs["blocked_visit"] = visit_page(session, EXCEPTED_HOST, "blocked")
    obs["page_count_samples"] = read_page_count(session)
    obs["request_blocked"] = read_boolean(session, BLOCKED_HISTOGRAM)
    obs["list_update_success"] = read_boolean(session, UPDATE_HISTOGRAM)
    open_manager(session)
    hits_before = custom_hits(session)
    send(session, "updateAll")
    # Settled: the custom list was fetched again and every list has either updated or failed.
    wait_hits = time.monotonic() + STATE_TIMEOUT
    while custom_hits(session) <= hits_before and time.monotonic() < wait_hits:
        time.sleep(0.25)
    state = wait_state(session, lambda s: all(x.get("updated") or x.get("error") for x in s["lists"]))
    obs["update"] = {"list_url": custom_url, "hits_before": hits_before, "hits_after": custom_hits(session),
                     "state": state}
    send(session, "setException", [EXCEPTED_HOST, True])
    state = wait_state(session, lambda s: EXCEPTED_HOST in s["exceptions"])
    obs["exception"] = {"state": state,
                        "excepted_visit": visit_page(session, EXCEPTED_HOST, "excepted"),
                        "other_visit": visit_page(session, OTHER_HOST, "other")}
    obs["validation"] = observe_validation(session, profile_path)
    return obs


# Each probe is a message the handler must refuse: (name, message, arguments).
VALIDATION_PROBES = (
    ("setException with a number as the host", "setException", [123, True]),
    ("setException with a 5000-character host", "setException", [LONG_ARG, True]),
    ("setException with a non-boolean switch", "setException", [EXCEPTED_HOST, "yes"]),
    ("addList with a 5000-character address", "addList", ["https://lists.bench.example/" + LONG_ARG]),
    ("removeList with a number as the id", "removeList", [123]),
    ("setAutoUpdate with a non-boolean value", "setAutoUpdate", ["yes"]),
)


def observe_validation(session: Session, profile_path: str | Path | None) -> dict:
    """Send arguments of the wrong type and length, and see that each is refused and ``state.json`` stays as
    it was (read after the write that the last real change caused has settled)."""
    open_manager(session)
    time.sleep(SETTLE)
    before = read_state_file(profile_path) if profile_path else None
    probes = [{"name": name, "refused": call(session, message, args).get("ok") is False}
              for name, message, args in VALIDATION_PROBES]
    time.sleep(SETTLE)
    after = read_state_file(profile_path) if profile_path else None
    return {"probes": probes, "state_before": before, "state_after": after}


def read_state_file(profile_path: str | Path) -> str | None:
    try:
        return (Path(profile_path) / STATE_FILE).read_text()
    except OSError:
        return None


def observe_second_launch(session: Session) -> dict:
    """After a restart on the same profile: the state, and one visit to each host."""
    state = open_manager(session)
    return {"state": state,
            "excepted_visit": visit_page(session, EXCEPTED_HOST, "restart-excepted"),
            "other_visit": visit_page(session, OTHER_HOST, "restart-other")}


@register
class FilterLists:
    name = "filter_lists"
    seeds_helium_services = False

    def run(self, session: Session) -> CheckResult:
        self._obs: dict = {}
        with session.new_profile() as profile:
            with session.variant((FLAG,), profile=profile) as first:
                self._obs = observe_first_launch(first, profile.path)
            self._first = first  # exited; first.netlog is the whole first launch
            self._obs["update"]["netlog_records"] = first.netlog
            self.run_second_launch(session, profile)
        return CheckResult(self.name, "pending")

    def run_second_launch(self, session: Session, profile) -> None:
        """Restart on the same profile; separate so it can be run or tested on its own."""
        with session.variant((FLAG,), profile=profile) as second:
            self._obs["restart"] = observe_second_launch(second)

    def finalize(self, session: Session, result: CheckResult) -> CheckResult:
        obs = getattr(self, "_obs", None)
        if not obs:
            return result  # run() failed; keep its error
        status, detail, data = judge(obs)
        recording = json.loads(json.dumps({**obs, "update": {**obs["update"], "netlog_records": [
            {k: r[k] for k in ("url", "urls", "scheme", "host") if k in r}
            for r in obs["update"].get("netlog_records", [])]}}, default=str))
        (session.out_dir / RECORDING_NAME).write_text(json.dumps(recording, indent=1) + "\n")
        session.artifacts.append(RECORDING_NAME)
        return CheckResult(self.name, status, detail, data=data)
