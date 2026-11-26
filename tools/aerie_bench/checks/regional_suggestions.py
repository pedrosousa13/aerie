"""Regional suggestions: a browser in another language suggests lists and turns none of them on (issue #106).

The browser starts on a fresh profile in a German locale (``LC_ALL=de_DE.UTF-8``; Chromium on Linux ignores
``--lang``, see ``LANG_ENV``). The Settings message handler (``AerieGuardSettingsHandler``)
answers ``aerieGuard.getState`` with ``suggested``, the catalogue ids that fit the locale and the language pref. The
brief is that a suggestion is only a suggestion: Brave turns a matching list on by itself, which for Aerie would fetch
from a publisher on a fresh profile and break zero traffic by default (issue #6).

What it proves, on one launch:

1. The match happened: ``suggested`` holds the German list (so the German locale reached the matching code), and every
   suggested id is a catalogue list for a language.
2. Nothing is on: no catalogue row is ``on`` in the state, none has left the ``idle`` status, and ``state.json`` (when
   the browser wrote one) has no entry that came from the catalogue.
3. Nothing was fetched: the NetLog of the launch holds no request to the host of any catalogue list, and the fixture
   server saw no request that the page did not ask for.

``zero_traffic`` on a profile in the same locale (``LC_ALL=de_DE.UTF-8 ./aerie bench --check zero_traffic``) is the
brief's other half. To see this step fail, run it on a build whose handler turns the match on.

The decision logic is in ``judge`` and takes recorded observations, like ``filter_lists``.
"""

from __future__ import annotations

import json
import time
from pathlib import Path
from urllib.parse import urlsplit

from .. import netlog
from ..cdp import CdpError
from ..session import CheckResult, Session
from . import register
from .filter_lists import SETTLE, open_manager, read_state_file

# Chromium on Linux with GLib reads the locale from LANGUAGE, LC_ALL, LC_MESSAGES and LANG only: the ``--lang``
# switch is ignored there (ui/base/l10n/l10n_util.cc, GetCandidates), so the check sets the environment.
LANG_ENV = {"LC_ALL": "de_DE.UTF-8"}
EXPECTED_ID = "easylistgermany"
CATALOGUE_FILE = Path(__file__).resolve().parents[3] / "overlay/components/aerie_adblock/catalogue.json"


def publisher_hosts(catalogue: list[dict]) -> set[str]:
    """The host of every catalogue download address."""
    return {urlsplit(e["url"]).hostname or "" for e in catalogue} - {""}


def catalogue_rows(state: dict | None) -> list[dict]:
    return [row for group in (state or {}).get("catalogue") or [] for row in group.get("lists") or []]


def judge(obs: dict | None) -> list[str]:
    """The problems with the observations of the German-locale launch. Empty means it passed."""
    if not obs:
        return ["no observations: the launch never answered getState"]
    state = obs.get("state")
    rows = catalogue_rows(state)
    problems: list[str] = []
    if not rows:
        problems.append("the state has no catalogue rows")
    suggested = (state or {}).get("suggested")
    if not isinstance(suggested, list) or EXPECTED_ID not in suggested:
        problems.append(f"a German locale did not suggest {EXPECTED_ID} (suggested: {suggested})")
    known = {row.get("id") for row in rows}
    for list_id in suggested if isinstance(suggested, list) else []:
        if list_id not in known:
            problems.append(f"suggested id {list_id} is not a catalogue list")
    on = [row.get("id") for row in rows if row.get("on")]
    if on:
        problems.append(f"catalogue list(s) turned on that nobody turned on: {', '.join(map(str, on))}")
    busy = [row.get("id") for row in rows if row.get("status") != "idle"]
    if busy:
        problems.append(f"catalogue list(s) not idle (a download started): {', '.join(map(str, busy))}")
    text = obs.get("state_file")
    if text:
        try:
            entries = (json.loads(text) or {}).get("lists") or []
        except ValueError:
            problems.append("state.json is not JSON")
            entries = []
        from_catalogue = [e.get("id") for e in entries if isinstance(e, dict) and e.get("catalogue")]
        if from_catalogue:
            problems.append(f"state.json holds catalogue list(s): {', '.join(map(str, from_catalogue))}")
    hosts = publisher_hosts(obs.get("catalogue") or [])
    if not hosts:
        problems.append("the catalogue was not read, so it is not proven that no publisher was contacted")
    seen = sorted({urlsplit(u).hostname or "" for u in netlog.all_urls(obs.get("netlog_records") or [])} & hosts)
    if seen:
        problems.append(f"the browser contacted a list publisher: {', '.join(seen)}")
    if obs.get("netlog_records") is None:
        problems.append("the NetLog is missing, so it is not proven that no publisher was contacted")
    before, after = obs.get("fixture_requests_before"), obs.get("fixture_requests_after")
    if before is None or after is None:
        problems.append("the fixture server's requests were not counted")
    elif after != before:
        problems.append(f"the fixture server saw {after - before} request(s) while only getState was sent")
    return problems


def observe(session: Session, profile_path: str | Path | None) -> dict:
    """Open Settings on the German-locale browser and read what it suggests and what it did about it."""
    obs: dict = {}
    obs["state"] = open_manager(session)
    before = len(session.fixtures.snapshot())
    time.sleep(SETTLE)  # a download the handler wrongly started would have reached the network by now
    obs["state_after"] = open_manager(session)
    obs["fixture_requests_before"], obs["fixture_requests_after"] = before, len(session.fixtures.snapshot())
    # What the page was given as the language: evidence that the flag reached the browser.
    obs["page_languages"] = session.evaluate("navigator.languages.join(',') + '|' + document.documentElement.lang")
    obs["state_file"] = read_state_file(profile_path) if profile_path else None
    return obs


@register
class RegionalSuggestions:
    name = "regional_suggestions"
    seeds_helium_services = False

    def run(self, session: Session) -> CheckResult:
        self._obs: dict = {}
        try:
            with session.new_profile() as profile:
                with session.variant((), profile=profile, env=LANG_ENV) as launch:
                    self._obs = observe(launch, profile.path)
                self._obs["netlog_records"] = launch.netlog  # filled in once the browser exited
        except (CdpError, OSError, RuntimeError) as exc:
            return CheckResult(self.name, "error", f"{type(exc).__name__}: {exc}")
        self._obs["catalogue"] = json.loads(CATALOGUE_FILE.read_text(encoding="utf-8"))
        return CheckResult(self.name, "pending")

    def finalize(self, session: Session, result: CheckResult) -> CheckResult:
        if not getattr(self, "_obs", None):
            return result  # run() failed; keep its error
        obs = self._obs
        # A late state: the one read after the quiet time is the one that judges (a list turned on by the match
        # would show here even when the first answer came before it).
        judged = {**obs, "state": obs.get("state_after") or obs.get("state")}
        problems = judge(judged)
        data = {"suggested": (judged["state"] or {}).get("suggested"), "page_languages": obs.get("page_languages")}
        if problems:
            return CheckResult(self.name, "fail", f"{len(problems)} problem(s): " + "; ".join(problems), data=data)
        return CheckResult(self.name, "pass",
                           f"A German locale suggested {', '.join(data['suggested'])}; no catalogue list is on, none "
                           f"is downloading, state.json has no catalogue entry and no list publisher was contacted",
                           data=data)
