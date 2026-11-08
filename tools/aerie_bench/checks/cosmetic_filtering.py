"""Cosmetic filtering: the Filter Engine hides leftover ad containers and leaves first-party content alone.

The fixture page (`fixtures/cosmetic/index.html`) has ad containers that carry
class names and ids from EasyList's generic hide rules, and first-party boxes
with ordinary names (one starts with "ad" on purpose). Its own CSS hides
nothing. `fixtures/cosmetic/expected.json` says which is which. The check reads
the layout over DevTools:

* each ad container: computed ``display`` is ``none`` and it has no box
  (``getBoundingClientRect`` is 0 by 0, no client rects);
* each first-party element: ``display`` is not ``none`` and it has a box;
* the hide is the Aerie engine's: the histogram ``Aerie.Adblock.CosmeticRuleCount``
  has at least one sample and a sum above zero. Nothing else on the page hides
  the boxes, but the signal also fails a browser whose stylesheet came from
  somewhere else;
* the page tries to take over the injection (it replaces ``CSSStyleSheet`` and
  the ``adoptedStyleSheets`` accessor in its own world). The browser injects
  from an isolated world, so the page must not see a call (``hijack_flag``) and
  the hide must still work;
* the hide is a user-origin sheet (ADR 0005), so the page cannot take it away. A few
  seconds after load the page empties ``adoptedStyleSheets``, removes its style
  elements and its ``<link rel=stylesheet>``, and forces ``display:block !important``
  on the ad boxes (case ``attacked``); they must still be hidden. The page also inserts ad containers after the load
  event, whose class and id were not on the page when the browser read it (case
  ``late``), and adds an ad class to an element that was already there (case
  ``late-class``); they must be hidden too. The page sets ``attack_flag`` and
  ``late_flag`` when it has done both: a run without them proves nothing and fails;
* the frame has one generic hide sheet, not one per pass (DOMContentLoaded, load
  and late batches). A user-origin sheet is not in ``adoptedStyleSheets``, so the
  rules come from the CSS domain: ``CSS.getMatchedStylesForNode`` on the probe
  element lists the matching rules with their origin. Blink reports a user-origin
  rule as ``user-agent`` (user rules are matched before any tree scope, so the
  CSSOM wrapper has no parent sheet and gets the inspector's default UA sheet),
  and the real UA sheet has no rule for the probe, so the check counts the
  ``user-agent`` rules whose selector is exactly ``generic_sheet_probe``, a
  selector that only the generic rules hide. Two sheets would match twice.

The decision logic is in plain functions (``judge``) that the tests run
against recorded measurements.
"""

from __future__ import annotations

import json
import time
from pathlib import Path

from ..fixtures import FIXTURE_DIR
from ..session import CheckResult, Session
from . import register

EXPECTED_FILE = FIXTURE_DIR / "cosmetic" / "expected.json"
RULE_COUNT_HISTOGRAM = "Aerie.Adblock.CosmeticRuleCount"
SETTLE_SECONDS = 20.0
PAGE_HOST = "news.localhost"


def load_expected(path: Path = EXPECTED_FILE) -> dict:
    data = json.loads(Path(path).read_text(encoding="utf-8"))
    for key in ("page", "hidden", "visible", "hijack_flag", "generic_sheet_probe", "attack_flag", "late_flag"):
        if not data.get(key):
            raise ValueError(f"{path}: needs {key!r}")
    for item in data["hidden"]:
        if item.get("case") not in ("attacked", "late", "late-class"):
            raise ValueError(f"{path}: hidden item {item.get('selector')!r} needs case "
                             "'attacked', 'late' or 'late-class'")
    return data


def measure_script(selectors: list[str]) -> str:
    """JS that reports, for each selector, whether and how its first element is laid out."""
    return ("(function(sels){return sels.map(function(s){"
            "var e=document.querySelector(s);if(!e)return {selector:s,found:false};"
            "var cs=getComputedStyle(e),r=e.getBoundingClientRect();"
            "return {selector:s,found:true,display:cs.display,width:r.width,height:r.height,"
            "rects:e.getClientRects().length};});})(" + json.dumps(selectors) + ")")


def count_probe_rules(matched: dict, probe: str) -> int:
    """Rules in a ``CSS.getMatchedStylesForNode`` reply that Blink reports as ``user-agent`` (where it puts
    user-origin rules, which are Aerie's hide sheets) and whose selector list holds exactly `probe`."""
    n = 0
    for entry in matched.get("matchedCSSRules") or []:
        rule = entry.get("rule") or {}
        if rule.get("origin") != "user-agent":
            continue
        texts = [s.get("text") for s in (rule.get("selectorList") or {}).get("selectors") or []]
        if texts == [probe]:
            n += 1
    return n


def histogram_totals(hist: dict | None) -> dict | None:
    """{"count": n, "sum": s} from a DevTools histogram, or None if the browser has none."""
    if not hist:
        return None
    return {"count": hist.get("count", 0), "sum": hist.get("sum", 0)}


def _is_hidden(m: dict) -> bool:
    return m["display"] == "none" and m["width"] == 0 and m["height"] == 0 and m["rects"] == 0


def _is_shown(m: dict) -> bool:
    return m["display"] != "none" and m["width"] > 0 and m["height"] > 0


def all_settled(expected: dict, measured: list[dict]) -> bool:
    """True once every ad container is hidden: the stylesheets arrive some time after the load event."""
    by_selector = {m["selector"]: m for m in measured}
    return all(by_selector.get(h["selector"], {}).get("found") and _is_hidden(by_selector[h["selector"]])
               for h in expected["hidden"])


CASE_WORDS = {
    "attacked": "after the page removed the sheet and forced display:block !important",
    "late": "late element, inserted after the load event",
    "late-class": "late class change on an element that was already there",
}


def judge(expected: dict, measured: list[dict], engine: dict | None, hijacked: bool,
          generic_sheets: int | None, page_flags: dict | None) -> tuple[str, str, dict]:
    """Return (status, detail, data). Every expectation that fails is named."""
    problems: list[str] = []
    by_selector = {m["selector"]: m for m in measured}
    hidden_rows, visible_rows = [], []
    for item in expected["hidden"]:
        sel = item["selector"]
        m = by_selector.get(sel)
        hidden_rows.append(m or {"selector": sel, "found": False})
        if m is None:
            problems.append(f"never measured: {sel}")
        elif not m["found"]:
            problems.append(f"ad container is not on the page: {sel}")
        elif not _is_hidden(m):
            what = CASE_WORDS[item["case"]]
            problems.append(f"NOT HIDDEN ({what}; display {m['display']}, {m['width']}x{m['height']}): {sel}")
    for item in expected["visible"]:
        sel = item["selector"]
        m = by_selector.get(sel)
        visible_rows.append(m or {"selector": sel, "found": False})
        if m is None:
            problems.append(f"never measured: {sel}")
        elif not m["found"]:
            problems.append(f"first-party element is not on the page: {sel}")
        elif not _is_shown(m):
            problems.append(f"HIDDEN BUT MUST STAY (display {m['display']}, {m['width']}x{m['height']}): {sel}")
    if engine is None:
        problems.append(f"no {RULE_COUNT_HISTOGRAM} histogram: this browser has no Aerie cosmetic filtering")
    elif engine["count"] < 1 or engine["sum"] <= 0:
        problems.append(f"the Aerie Filter Engine gave no hide rules ({RULE_COUNT_HISTOGRAM}: {engine['count']} "
                        f"sample(s), sum {engine['sum']}); whatever hid the boxes was not the engine")
    if hijacked:
        problems.append(f"the page's own script saw the injection (window.{expected['hijack_flag']} is set)")
    if generic_sheets is None:
        problems.append("could not count the frame's generic hide sheets")
    elif generic_sheets != 1:
        problems.append(f"the frame has {generic_sheets} generic hide sheets, expected exactly one")
    if page_flags is None:
        problems.append("could not read whether the page ran its attack and its late insert")
    else:
        if not page_flags.get("attack_done"):
            problems.append(f"the page never ran its attack (window.{expected['attack_flag']} is unset): "
                            "the removed-sheet case proves nothing")
        if not page_flags.get("late_inserted"):
            problems.append(f"the page never inserted its late elements (window.{expected['late_flag']} is unset): "
                            "the late-insert case proves nothing")
    data = {"page_flags": page_flags, "hidden": hidden_rows, "visible": visible_rows, "engine": engine, "hijacked": hijacked,
            "generic_sheets": generic_sheets}
    if problems:
        return "fail", f"{len(problems)} problem(s): " + "; ".join(problems), data
    n_hidden, n_visible = len(expected["hidden"]), len(expected["visible"])
    return "pass", (f"{n_hidden}/{n_hidden} ad containers hidden, {n_visible}/{n_visible} first-party elements "
                    "visible, hide rules from the Aerie engine, injection not visible to the page, one generic hide sheet; "
                    "ads stayed hidden after the page removed the sheet and forced display:block !important, "
                    "ad containers inserted late were hidden, and so was an element with an ad class added later"), data


@register
class CosmeticFiltering:
    name = "cosmetic_filtering"
    seeds_helium_services = False

    def __init__(self, settle_seconds: float = SETTLE_SECONDS, expected_path: str | None = None):
        self.settle_seconds = settle_seconds
        self.expected = load_expected(Path(expected_path) if expected_path else EXPECTED_FILE)

    def _measure(self, session: Session) -> list[dict]:
        selectors = [i["selector"] for i in self.expected["hidden"] + self.expected["visible"]]
        return session.evaluate(measure_script(selectors)) or []

    def _count_generic_sheets(self, session: Session) -> int | None:
        """Injected generic rules that match the probe element, from the CSS domain; None if that fails."""
        probe = self.expected["generic_sheet_probe"]
        try:
            session.cdp("DOM.enable")
            session.cdp("CSS.enable")
            root = session.cdp("DOM.getDocument", {"depth": 0})["root"]["nodeId"]
            node = session.cdp("DOM.querySelector", {"nodeId": root, "selector": probe})["nodeId"]
            if not node:
                return None
            matched = session.cdp("CSS.getMatchedStylesForNode", {"nodeId": node})
        except Exception:
            return None
        return count_probe_rules(matched, probe)

    def _page_flags(self, session: Session) -> dict | None:
        try:
            return {"attack_done": bool(session.evaluate(f"window.{self.expected['attack_flag']} === true")),
                    "late_inserted": bool(session.evaluate(f"window.{self.expected['late_flag']} === true"))}
        except Exception:
            return None

    def run(self, session: Session) -> CheckResult:
        # Not 127.0.0.1: adblock-rust gives a bare IP address or "localhost" no hide rules at all. A
        # *.localhost name has a site of its own, resolves to the fixture server and is not upgraded
        # by HTTPS-only mode.
        nav = session.navigate_url(f"http://{PAGE_HOST}:{session.fixtures.port}{self.expected['page']}")
        if nav["error"]:
            return CheckResult(self.name, "fail", f"could not load the fixture page: {nav['error']}")
        # The page attacks the hide a few seconds after load and inserts late ads shortly after. Wait for both,
        # then for the hide to settle: a hide that came before the attack and then gave way must be seen.
        deadline = time.monotonic() + self.settle_seconds
        while True:
            page_flags = self._page_flags(session)
            if (page_flags and page_flags["attack_done"] and page_flags["late_inserted"]) \
                    or time.monotonic() > deadline:
                break
            time.sleep(0.2)
        while True:
            measured = self._measure(session)
            if all_settled(self.expected, measured) or time.monotonic() > deadline:
                break
            time.sleep(0.2)
        # The attack must not win later either: look again after a quiet moment.
        time.sleep(0.5)
        measured = self._measure(session)
        generic_sheets = self._count_generic_sheets(session)
        hijacked = bool(session.evaluate(f"window.{self.expected['hijack_flag']} === true"))
        session.screenshot("cosmetic_filtering.png")
        try:
            hist = session.client.send("Browser.getHistogram", {"name": RULE_COUNT_HISTOGRAM},
                                       timeout=session.timeout)["histogram"]
        except Exception:
            hist = None
        status, detail, data = judge(self.expected, measured, histogram_totals(hist), hijacked, generic_sheets, page_flags)
        return CheckResult(self.name, status, detail, data=data)
