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
    "aerie_guard": {"url": "chrome://settings/aerieGuard", "ready": ".list-row"},
    "hardening": {"url": "chrome://settings/privacy/hardening", "ready": ".control"},
    "appearance": {"url": "chrome://settings/appearance", "ready": "#aerieMotionRow", "only": "#aerieMotionRow"},
}

MEASURE_JS = """(only, breakIndex) => {
  const rows = deepQueryAll(document, '.cr-row, .settings-box')
      .filter(r => (!only || r.matches(only)) && r.getClientRects().length > 0);
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


def measure(session: Session, surface: dict, break_index: int | None = None) -> list[dict]:
    only = json.dumps(surface.get("only"))
    index = "null" if break_index is None else str(break_index)
    expr = f"(() => {{ {DEEP_JS}\n return ({MEASURE_JS})({only}, {index}); }})()"
    return session.evaluate(expr) or []


def row_problems(rows: list[dict]) -> list[str]:
    problems = []
    for row in rows:
        token = row["token"]
        label = f"row {row['label']!r}"
        if not token:
            problems.append(f"{label}: --cr-section-padding is not set on it")
            continue
        if abs(row["start"] - token) > TOLERANCE:
            problems.append(f"{label}: inline-start padding {row['start']}px, token {token}px")
        if abs(row["end"] - token) > TOLERANCE:
            problems.append(f"{label}: inline-end padding {row['end']}px, token {token}px")
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
        for name, surface in SURFACES.items():
            session.navigate_url(surface["url"])
            ready = surface["ready"]
            found = session.wait_for(ready_js(ready), timeout=20)
            obs["surfaces"][name] = {"ready": bool(found), "rows": measure(session, surface) if found else []}
        session.navigate_url(SURFACES["aerie_guard"]["url"])
        session.wait_for(ready_js(".list-row"), timeout=20)
        obs["control_rows"] = measure(session, SURFACES["aerie_guard"], break_index=0)
        return obs

    @staticmethod
    def judge(obs: dict) -> Verdict:
        control = row_problems(obs.get("control_rows") or [])
        if len(control) < 1:
            return "fail", "negative control: a row with padding 0 was not reported, so the check cannot see a missing padding"
        failures = []
        for name, data in (obs.get("surfaces") or {}).items():
            if not data.get("ready") or not data.get("rows"):
                failures.append(f"{name}: no rows could be read")
                continue
            failures += [f"{name}: {p}" for p in row_problems(data["rows"])]
        if failures:
            return "fail", "; ".join(failures[:6]) + (f" (and {len(failures) - 6} more)" if len(failures) > 6 else "")
        total = sum(len(d["rows"]) for d in obs["surfaces"].values())
        return "pass", f"{total} rows on {len(obs['surfaces'])} Aerie Settings surfaces keep the standard inline padding"
