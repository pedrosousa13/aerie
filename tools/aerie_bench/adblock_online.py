"""The opt-in online run of ``adblock_score`` (``--adblock-online SEC``): read the live test page.

A browser loads https://adblock.turtlecute.org, waits for the page's own test to finish and the score and
per-check results are read from the page's markup. The run is informational: it never fails a train, because
the site can change. A page that does not load, never finishes or no longer has the markup read here is
reported as such (``state``), not raised.

What is read (the page's markup as of 2026-11-09, from its source, github.com/Turtlecute33/adblocktest):

* ``#adb_test_r``: "Total: N", "M blocked", "K not blocked" once the test is done (the numbers are missing
  before). N counts every host check and the four page checks.
* ``#ct_static``, ``#ct_dynamic`` (cosmetic) and ``#sfa_1``, ``#sfa_2`` (decoy ads.js and pagead.js): class
  ``_bg-green`` is blocked, ``_bg-red`` is not.
* ``#test``: one ``.grid`` (id = category) per category, a ``.test`` (id = vendor) per vendor, and one ``div``
  per host. A vendor with a host that got through has ``data-has-failure="true"``. The page marks the result of
  each host with an icon only, so the per-host result is not read; the vendor flag is.
"""

from __future__ import annotations

import re
import time
from html.parser import HTMLParser

LIVE_URL = "https://adblock.turtlecute.org/"
PAGE_CHECKS = {"cosmetic_static": "ct_static", "cosmetic_dynamic": "ct_dynamic",
               "script_ads": "sfa_1", "script_pagead": "sfa_2"}
_VOID = {"area", "base", "br", "col", "embed", "hr", "img", "input", "link", "meta", "source", "track", "wbr"}
_COUNTS = re.compile(r"Total:\s*(\d+)\D+?(\d+)\s*blocked\D+?(\d+)\s*not blocked", re.IGNORECASE)


class _Node:
    def __init__(self, tag: str, attrs: dict, parent: "_Node | None") -> None:
        self.tag, self.attrs, self.parent = tag, attrs, parent
        self.children: list[_Node | str] = []

    @property
    def classes(self) -> list[str]:
        return (self.attrs.get("class") or "").split()

    def walk(self):
        for child in self.children:
            if isinstance(child, _Node):
                yield child
                yield from child.walk()

    def text(self) -> str:
        return " ".join(c if isinstance(c, str) else c.text() for c in self.children)


class _Builder(HTMLParser):
    def __init__(self) -> None:
        super().__init__(convert_charrefs=True)
        self.root = _Node("#root", {}, None)
        self.current = self.root

    def handle_starttag(self, tag, attrs):
        node = _Node(tag, {k: (v or "") for k, v in attrs}, self.current)
        self.current.children.append(node)
        if tag not in _VOID:
            self.current = node

    def handle_startendtag(self, tag, attrs):
        self.current.children.append(_Node(tag, {k: (v or "") for k, v in attrs}, self.current))

    def handle_endtag(self, tag):
        node = self.current
        while node is not self.root and node.tag != tag:
            node = node.parent
        if node is not self.root:  # an end tag with no open start tag is ignored
            self.current = node.parent

    def handle_data(self, data):
        self.current.children.append(data)


def _tree(html: str) -> _Node:
    builder = _Builder()
    builder.feed(html)
    builder.close()
    return builder.root


def _by_id(root: _Node, ident: str) -> _Node | None:
    return next((n for n in root.walk() if n.attrs.get("id") == ident), None)


def _page_check(node: _Node | None) -> bool | None:
    """True: blocked (green). False: got through (red). None: not decided yet or not on the page."""
    if node is None:
        return None
    if "_bg-green" in node.classes:
        return True
    if "_bg-red" in node.classes:
        return False
    return None


def parse_page(html: str) -> dict:
    """What the page's markup says. ``finished`` is true only when the result card holds numbers that add up."""
    root = _tree(html)
    out: dict = {"finished": False, "total": None, "blocked": None, "not_blocked": None, "percent": None,
                 "checks": {name: _page_check(_by_id(root, ident)) for name, ident in PAGE_CHECKS.items()},
                 "categories": {}}
    card = _by_id(root, "adb_test_r")
    counts = _COUNTS.search(re.sub(r"\s+", " ", card.text())) if card is not None else None
    if counts:
        total, blocked, not_blocked = (int(x) for x in counts.groups())
        out.update(total=total, blocked=blocked, not_blocked=not_blocked)
        if blocked + not_blocked != total or total == 0:
            out["problem"] = f"the numbers do not add up: {blocked} blocked + {not_blocked} not blocked, total {total}"
        else:
            out["finished"] = True
            out["percent"] = round(100 * blocked / total, 1)
    wrapper = _by_id(root, "test")
    for grid in (wrapper.walk() if wrapper is not None else ()):
        if grid.tag != "div" or "grid" not in grid.classes or not grid.attrs.get("id"):
            continue
        vendors = [n for n in grid.walk() if n.tag == "div" and "test" in n.classes]
        out["categories"][grid.attrs["id"]] = {
            "vendors": len(vendors),
            "hosts": sum(1 for v in vendors for s in v.walk() if s.tag == "span" and "test_collapse" not in s.classes),
            "vendors_with_failure": [v.attrs.get("id", "") for v in vendors if v.attrs.get("data-has-failure") == "true"],
        }
    return out


def read_live(session, wait_seconds: float, poll_seconds: float = 1.0) -> dict:
    """Load the live page and read the result. Always returns a dict with a ``state``:
    ``finished``, ``timeout`` (the test did not finish in time), ``load_failed`` or ``error``."""
    out: dict = {"url": LIVE_URL}
    try:
        loaded = session.navigate_url(LIVE_URL, wait=wait_seconds)
        if loaded.get("error"):
            return {**out, "state": "load_failed", "detail": f"the page did not load: {loaded['error']}"}
        deadline = time.monotonic() + wait_seconds
        parsed: dict = {}
        while True:
            parsed = parse_page(session.evaluate("document.documentElement.outerHTML") or "")
            if parsed["finished"]:
                return {**out, "state": "finished", **parsed}
            if time.monotonic() >= deadline:
                break
            time.sleep(poll_seconds)
        why = parsed.get("problem") or "the result card never showed numbers"
        return {**out, "state": "timeout", "detail": f"the test did not finish within {wait_seconds:g} s ({why})", **parsed}
    except Exception as exc:  # informational: whatever goes wrong is data, never a failure of the train
        return {**out, "state": "error", "detail": f"{type(exc).__name__}: {exc}"}
