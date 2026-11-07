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
  the hide must still work.

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
SETTLE_SECONDS = 15.0


def load_expected(path: Path = EXPECTED_FILE) -> dict:
    data = json.loads(Path(path).read_text(encoding="utf-8"))
    for key in ("page", "hidden", "visible", "hijack_flag"):
        if not data.get(key):
            raise ValueError(f"{path}: needs {key!r}")
    return data


def measure_script(selectors: list[str]) -> str:
    """JS that reports, for each selector, whether and how its first element is laid out."""
    return ("(function(sels){return sels.map(function(s){"
            "var e=document.querySelector(s);if(!e)return {selector:s,found:false};"
            "var cs=getComputedStyle(e),r=e.getBoundingClientRect();"
            "return {selector:s,found:true,display:cs.display,width:r.width,height:r.height,"
            "rects:e.getClientRects().length};});})(" + json.dumps(selectors) + ")")


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


def judge(expected: dict, measured: list[dict], engine: dict | None, hijacked: bool) -> tuple[str, str, dict]:
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
            problems.append(f"NOT HIDDEN (display {m['display']}, {m['width']}x{m['height']}): {sel}")
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
    data = {"hidden": hidden_rows, "visible": visible_rows, "engine": engine, "hijacked": hijacked}
    if problems:
        return "fail", f"{len(problems)} problem(s): " + "; ".join(problems), data
    n_hidden, n_visible = len(expected["hidden"]), len(expected["visible"])
    return "pass", (f"{n_hidden}/{n_hidden} ad containers hidden, {n_visible}/{n_visible} first-party elements "
                    "visible, hide rules from the Aerie engine, injection not visible to the page"), data


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

    def run(self, session: Session) -> CheckResult:
        session.navigate(self.expected["page"])
        deadline = time.monotonic() + self.settle_seconds
        while True:
            measured = self._measure(session)
            if all_settled(self.expected, measured) or time.monotonic() > deadline:
                break
            time.sleep(0.2)
        hijacked = bool(session.evaluate(f"window.{self.expected['hijack_flag']} === true"))
        session.screenshot("cosmetic_filtering.png")
        try:
            hist = session.client.send("Browser.getHistogram", {"name": RULE_COUNT_HISTOGRAM},
                                       timeout=session.timeout)["histogram"]
        except Exception:
            hist = None
        status, detail, data = judge(self.expected, measured, histogram_totals(hist), hijacked)
        return CheckResult(self.name, status, detail, data=data)
