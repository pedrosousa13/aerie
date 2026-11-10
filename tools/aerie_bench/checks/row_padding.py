"""Every row on an Aerie-owned Settings page has the standard inline padding (issue #205).

The check opens each Aerie Settings surface, finds every row (``.cr-row`` and ``.settings-box``, in every
shadow root) and reads three numbers from the browser: the computed inline-start and inline-end padding of
the row, and how far its first child starts from the row's start edge. The token is
``--cr-section-padding`` as the browser resolves it on that row. A row passes when both paddings equal the
token and the first child starts at least that far in. A row with no padding, or with text on the card
edge, fails and is named.

Surfaces: the Aerie Guard page (filter lists, sites where blocking is off, own rules), the Security
hardening page and the Appearance page (Reduce motion row, which is the only row measured there: the
other rows are Helium's).

One more surface has its own layout, not Settings rows: the setup page (``chrome://setup/``, on its search
step, where the option cards are). Its cards (``card`` surface) are read the same way, but a card has no
``--cr-section-padding`` to match: it passes when both inline paddings are at least ``CARD_MIN_PADDING`` and
its first child starts at least as far in as its own start padding. The Aerie Guard panel is not measured
here: its page draws nothing until a bubble has captured a tab, so ``aerie_guard_panel`` is the check that
opens it.

Neighbours: Helium's own rows on the Appearance page give the padding an Aerie row must match, as well as
the token. The Aerie Guard page must also contain the own-rules row (#107), and a filter list row.

Negative control: on the Aerie Guard page the check sets ``padding: 0`` on one real row and reads again.
The judge must report that row, or the check has no way to see a missing padding.

``observe`` drives the browser and ``judge`` turns the data into a verdict, as the hardening checks do.
"""

from __future__ import annotations

import json

from ..session import Session
from . import register
from .hardening.common import DEEP_JS, HardeningCheck, Verdict

TOLERANCE = 0.75  # px; sub-pixel layout and zoom

SURFACES = {
    "aerie_guard": {"url": "chrome://settings/aerieGuard", "ready": ".list-row",
                    "required": [".custom-rules-row", ".list-row"]},
    # #headerLine is the title row of Helium's own subpage frame (back arrow and heading), not an Aerie row.
    "hardening": {"url": "chrome://settings/privacy/hardening", "ready": ".control", "exclude": "#headerLine"},
    "appearance": {"url": "chrome://settings/appearance", "ready": "#aerieMotionRow", "only": "#aerieMotionRow"},
    "setup": {"url": "chrome://setup/", "ready": ".opt", "card": True, "rows": ".opt", "setup_step": "search"},
}

CARD_MIN_PADDING = 8  # px; the smallest inline padding a card of the panel or setup page may have

# Helium's own rows, read on the Appearance page: the built-in neighbours an Aerie row must line up with.
NEIGHBOURS = {"url": "chrome://settings/appearance", "ready": ".settings-box, .cr-row", "exclude": "#aerieMotionRow"}

# Entrance animations move a card by a few pixels (transform) while it fades in; a read mid-animation would
# report that offset as a missing inset, so every finite animation is finished first.
SETTLE_JS = "document.getAnimations().forEach(a => { try { a.finish(); } catch (e) {} });"

MEASURE_JS = """(only, breakIndex, exclude, rowSel) => {
  const rows = deepQueryAll(document, rowSel)
      .filter(r => (!only || r.matches(only)) && !(exclude && r.matches(exclude)) && r.getClientRects().length > 0);
  // Padding is layout: a card or a child that a CSS transform scales or nudges (the selected setup option
  // turns its logo by 4 degrees and grows it) would shift its rectangle, so the transforms are dropped on the
  // row and on its children before they are read.
  rows.forEach(r => [r, ...r.children].forEach(e => { e.style.transition = 'none'; e.style.transform = 'none'; }));
  if (breakIndex !== null && rows[breakIndex]) rows[breakIndex].style.padding = '0';
  const rtl = getComputedStyle(document.documentElement).direction === 'rtl';
  return rows.map((row, i) => {
    const cs = getComputedStyle(row);
    const probe = document.createElement('div');
    probe.style.cssText = 'position:absolute;visibility:hidden;width:var(--cr-section-padding)';
    row.appendChild(probe);
    const token = probe.getBoundingClientRect().width;
    probe.remove();
    const box = row.getBoundingClientRect();
    const first = [...row.children].find(c => c.getClientRects().length > 0);
    const fb = first ? first.getBoundingClientRect() : null;
    return {
      card: rowSel !== '.cr-row, .settings-box',
      index: i,
      label: (row.id || row.className || row.tagName).toString().slice(0, 60),
      token,
      start: parseFloat(rtl ? cs.paddingRight : cs.paddingLeft),
      end: parseFloat(rtl ? cs.paddingLeft : cs.paddingRight),
      inset: fb ? (rtl ? box.right - fb.right : fb.left - box.left) : null,
    };
  });
}"""


def ready_js(selector: str) -> str:
    return f"(() => {{ {DEEP_JS}\n return !!deepQuery(document, {json.dumps(selector)}); }})()"


def open_setup_step(session: Session, step: str, max_clicks: int = 6) -> None:
    """Press the setup page's Continue button until ``step`` shows. If it never does, the ready selector fails
    the surface and the check names it."""
    from .setup_theme import STEP_JS
    session.navigate_url(SURFACES["setup"]["url"])
    if not session.wait_for("!!window.aerieSetup", timeout=20):
        return
    for _ in range(max_clicks):
        current = session.evaluate(STEP_JS)
        if current == step or not session.wait_for("!!document.querySelector('#next')", timeout=10):
            return
        session.click("#next")
        session.wait_for(f"{STEP_JS} !== {json.dumps(current)}", timeout=10)


def measure(session: Session, surface: dict, break_index: int | None = None) -> list[dict]:
    only = json.dumps(surface.get("only"))
    exclude = json.dumps(surface.get("exclude"))
    row_sel = json.dumps(surface.get("rows", ".cr-row, .settings-box"))
    index = "null" if break_index is None else str(break_index)
    expr = f"(() => {{ {DEEP_JS}\n{SETTLE_JS}\n return ({MEASURE_JS})({only}, {index}, {exclude}, {row_sel}); }})()"
    return session.evaluate(expr) or []


def neighbour_padding(rows: list[dict]) -> float | None:
    """The inline padding Helium's own rows use: the smallest start value of its ordinary rows.

    A ``continuation`` row is a second line inside a row and has no start padding by design, so it is left
    out; read on a real build it was the one row that put the value at 0."""
    values = [r["start"] for r in rows if r.get("start") is not None and "continuation" not in r.get("label", "")]
    return min(values) if values else None


def row_problems(rows: list[dict], neighbour: float | None = None) -> list[str]:
    problems = []
    for row in rows:
        token = row["token"]
        label = f"row {row['label']!r}"
        if row.get("card"):
            for side in ("start", "end"):
                if row[side] < CARD_MIN_PADDING - TOLERANCE:
                    problems.append(f"{label}: inline-{side} padding {row[side]}px, card minimum {CARD_MIN_PADDING}px")
            if row["inset"] is not None and row["inset"] < row["start"] - TOLERANCE:
                problems.append(f"{label}: content starts {row['inset']}px from the edge, padding {row['start']}px")
            continue
        if not token:
            problems.append(f"{label}: --cr-section-padding is not set on it")
            continue
        if abs(row["start"] - token) > TOLERANCE:
            problems.append(f"{label}: inline-start padding {row['start']}px, token {token}px")
        if abs(row["end"] - token) > TOLERANCE:
            problems.append(f"{label}: inline-end padding {row['end']}px, token {token}px")
        if neighbour is not None:
            for side in ("start", "end"):
                if abs(row[side] - neighbour) > TOLERANCE:
                    problems.append(f"{label}: inline-{side} padding {row[side]}px, built-in neighbour rows {neighbour}px")
        if row["inset"] is not None and row["inset"] < token - TOLERANCE:
            problems.append(f"{label}: content starts {row['inset']}px from the edge, token {token}px")
    return problems


@register
class RowPadding(HardeningCheck):
    name = "row_padding"
    control = "all"
    negative_control = ("One real row on the Aerie Guard page gets padding 0 and is read again; the judge must "
                        "name it, so a missing row padding cannot pass unseen.")

    def observe(self, session: Session) -> dict:
        obs: dict = {"surfaces": {}}
        session.navigate_url(NEIGHBOURS["url"])
        found = session.wait_for(ready_js(NEIGHBOURS["ready"]), timeout=20)
        obs["neighbours"] = measure(session, NEIGHBOURS) if found else []
        for name, surface in SURFACES.items():
            if surface.get("setup_step"):
                open_setup_step(session, surface["setup_step"])
            else:
                session.navigate_url(surface["url"])
            ready = surface["ready"]
            found = session.wait_for(ready_js(ready), timeout=20)
            missing = [sel for sel in surface.get("required", []) if not session.wait_for(ready_js(sel), timeout=5)] if found else []
            obs["surfaces"][name] = {"ready": bool(found), "missing": missing,
                                     "rows": measure(session, surface) if found else []}
        session.navigate_url(SURFACES["aerie_guard"]["url"])
        session.wait_for(ready_js(".list-row"), timeout=20)
        obs["control_rows"] = measure(session, SURFACES["aerie_guard"], break_index=0)
        return obs

    @staticmethod
    def judge(obs: dict) -> Verdict:
        neighbour = neighbour_padding(obs.get("neighbours") or [])
        if neighbour is None:
            return "fail", "no built-in neighbour rows could be read, so Aerie rows cannot be compared with them"
        control = row_problems(obs.get("control_rows") or [], neighbour)
        if not control:
            return "fail", "negative control: a row with padding 0 was not reported, so the check cannot see a missing padding"
        failures = []
        for name, data in (obs.get("surfaces") or {}).items():
            if not data.get("ready") or not data.get("rows"):
                failures.append(f"{name}: no rows could be read")
                continue
            failures += [f"{name}: no row matches {sel} (a row the check must measure)" for sel in data.get("missing", [])]
            failures += [f"{name}: {p}" for p in row_problems(data["rows"], neighbour)]
        if failures:
            return "fail", "; ".join(failures[:6]) + (f" (and {len(failures) - 6} more)" if len(failures) > 6 else "")
        total = sum(len(d["rows"]) for d in obs["surfaces"].values())
        return "pass", f"{total} rows on {len(obs['surfaces'])} Aerie surfaces keep the standard inline padding and match the built-in rows ({neighbour}px)"
