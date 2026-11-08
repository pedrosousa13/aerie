"""Performance metrics: thresholds, comparison, history and reports (issue #16).

Pure logic, no browser. The ``perf`` check (checks/perf.py) measures; this module judges.

A metrics document (``perf.json``) is::

    {"schema": 1, "label": "...", "binary": {"path": ..., "version": ...}, "started": ISO 8601,
     "metrics": {"memory_after_ad_pages_mb": 812.4, ...},
     "skipped": {"jetstream_score": "reason it was switched off"},
     "failed": {"speedometer_score": "reason it could not be measured"}}

A run with anything in ``failed`` fails the perf check and is never recorded as a baseline.

Thresholds and the reasons for them are in ``METRICS``; docs/bench.md prints the same table.
"""

from __future__ import annotations

import json
import os
from dataclasses import dataclass
from pathlib import Path

SCHEMA = 1
HISTORY_FILE = "history.jsonl"
MAIN_FILE = "main.json"


@dataclass(frozen=True)
class Metric:
    unit: str
    better: str            # "lower" or "higher"
    max_regression_pct: float
    min_abs_change: float  # a change smaller than this is noise, whatever the percentage
    required: bool         # a run that cannot measure it fails
    rationale: str


METRICS: dict[str, Metric] = {
    "memory_after_ad_pages_mb": Metric(
        "MB", "lower", 10.0, 30.0, True,
        "Resident memory of the whole process tree after the ad-heavy fixture pages load and settle. "
        "Repeated runs of one build differ by a few percent; 10% and 30 MB is outside that and still "
        "catches a leak or a cache that grew."),
    "startup_cold_ms": Metric(
        "ms", "lower", 25.0, 200.0, True,
        "Spawn to first page load on an empty profile. Disk and CPU load make it the noisiest metric, "
        "and a launch is a few seconds, so the margin is wide."),
    "startup_warm_ms": Metric(
        "ms", "lower", 25.0, 150.0, True,
        "Spawn to first page load when the profile already ran once. Same noise as cold start."),
    "speedometer_score": Metric(
        "score", "higher", 7.0, 0.0, True,
        "Speedometer 3.1, run locally from the pinned copy in the build root. Scores move a few percent "
        "between runs. A run that cannot measure it fails."),
    "jetstream_score": Metric(
        "score", "higher", 7.0, 0.0, True,
        "JetStream 3, run locally from the pinned copy in the build root. Same noise as Speedometer. "
        "A run that cannot measure it fails."),
}


def regressed(metric: Metric, current: float, baseline: float) -> bool:
    """Worse than the baseline by more than the percentage and by more than the absolute floor."""
    worse = current - baseline if metric.better == "lower" else baseline - current
    if worse <= metric.min_abs_change:
        return False
    if baseline == 0:
        return worse > 0
    return worse / abs(baseline) * 100.0 > metric.max_regression_pct


def compare(current: dict, baseline: dict, metrics: dict[str, Metric] = METRICS) -> list[dict]:
    """One verdict per metric known to either document.

    status: ``ok``, ``regressed``, ``improved``, ``new`` (no baseline value), ``not_measured``
    (baseline has it, this run skipped it on request) and ``missing`` (a required metric this run
    lacks without having been told to skip it).
    """
    cur, base = current.get("metrics", {}), baseline.get("metrics", {})
    out = []
    for name in sorted(set(cur) | set(base), key=_order):
        spec = metrics.get(name)
        c, b = cur.get(name), base.get(name)
        row = {"metric": name, "current": c, "baseline": b, "change_pct": None, "status": "ok"}
        if c is None:
            skipped = name in (current.get("skipped") or {})
            row["status"] = "missing" if spec and spec.required and not skipped else "not_measured"
        elif b is None:
            row["status"] = "new"
        else:
            row["change_pct"] = round((c - b) / abs(b) * 100.0, 1) if b else None
            if spec is None:
                pass
            elif regressed(spec, c, b):
                row["status"] = "regressed"
            elif regressed(spec, b, c):
                row["status"] = "improved"
        out.append(row)
    return out


def failures(verdicts: list[dict]) -> list[dict]:
    return [v for v in verdicts if v["status"] in ("regressed", "missing")]


def _order(name: str) -> tuple[int, str]:
    names = list(METRICS)
    return (names.index(name) if name in names else len(names), name)


def _num(value) -> str:
    if value is None:
        return "-"
    return f"{value:.1f}"


def verdict_lines(verdicts: list[dict]) -> list[str]:
    lines = []
    for v in verdicts:
        spec = METRICS.get(v["metric"])
        unit = spec.unit if spec else ""
        change = "" if v["change_pct"] is None else f" ({v['change_pct']:+.1f}%)"
        limit = f", limit {spec.max_regression_pct:g}% and {spec.min_abs_change:g} {unit}" if spec else ""
        shown = f"{_num(v['current'])} {unit}".rstrip()
        lines.append(f"{v['status'].upper():12} {v['metric']}: {shown} against {_num(v['baseline'])}{change}{limit}")
    return lines


# --- documents, history -------------------------------------------------------------------------

def load(path: Path) -> dict:
    doc = json.loads(Path(path).read_text())
    if not isinstance(doc, dict) or doc.get("schema") != SCHEMA or not isinstance(doc.get("metrics"), dict):
        raise ValueError(f"{path}: not a schema {SCHEMA} perf document")
    return doc


def record(directory: Path, doc: dict) -> Path:
    """Append ``doc`` to the series and make it the baseline (main.json) for the next comparison."""
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    with open(directory / HISTORY_FILE, "a") as f:
        f.write(json.dumps(doc, sort_keys=True) + "\n")
    tmp = directory / (MAIN_FILE + ".tmp")
    tmp.write_text(json.dumps(doc, indent=1, sort_keys=True) + "\n")
    os.replace(tmp, directory / MAIN_FILE)
    return directory / MAIN_FILE


def read_history(directory: Path) -> list[dict]:
    path = Path(directory) / HISTORY_FILE
    try:
        lines = path.read_text().splitlines()
    except OSError:
        return []
    docs = []
    for line in lines:
        try:
            doc = json.loads(line)
        except ValueError:
            continue  # a torn last line from an interrupted write
        if isinstance(doc, dict) and isinstance(doc.get("metrics"), dict):
            docs.append(doc)
    return docs


# --- reports --------------------------------------------------------------------------------------

def _heading(name: str) -> str:
    spec = METRICS.get(name)
    return f"{name} ({spec.unit})" if spec else name


def series_report(history: list[dict]) -> str:
    """Markdown table, one row per recorded build, oldest first, with the change from the row above."""
    names = sorted({n for d in history for n in d["metrics"]}, key=_order)
    if not history:
        return "No recorded builds.\n"
    lines = ["| build | started | " + " | ".join(_heading(n) for n in names) + " |",
             "| --- | --- | " + " | ".join("---:" for _ in names) + " |"]
    prev: dict = {}
    for d in history:
        cells = []
        for n in names:
            v = d["metrics"].get(n)
            cell = _num(v)
            if v is not None and prev.get(n):
                cell += f" ({(v - prev[n]) / abs(prev[n]) * 100.0:+.1f}%)"
            cells.append(cell)
        prev = {**prev, **d["metrics"]}
        label = d.get("label") or (d.get("binary") or {}).get("version") or "?"
        lines.append(f"| {label} | {d.get('started', '?')} | " + " | ".join(cells) + " |")
    return "\n".join(lines) + "\n"


def side_by_side(aerie: dict, other: dict, other_name: str = "Helium") -> str:
    """Markdown table of two documents. The difference is relative to the other browser."""
    names = sorted(set(aerie["metrics"]) | set(other["metrics"]), key=_order)
    lines = [f"| metric | Aerie | {other_name} | Aerie vs {other_name} |", "| --- | ---: | ---: | ---: |"]
    for n in names:
        a, o = aerie["metrics"].get(n), other["metrics"].get(n)
        diff = "-" if a is None or not o else f"{(a - o) / abs(o) * 100.0:+.1f}%"
        lines.append(f"| {_heading(n)} | {_num(a)} | {_num(o)} | {diff} |")
    for who, doc in (("Aerie", aerie), (other_name, other)):
        for n, why in sorted(doc.get("skipped", {}).items()):
            lines.append(f"\n{who}: {n} not measured: {why}")
    lines += ["", f"Aerie: {(aerie.get('binary') or {}).get('version') or 'unknown'}, "
                  f"{other_name}: {(other.get('binary') or {}).get('version') or 'unknown'}"]
    lines.append("For memory and startup lower is better; for scores higher is better.")
    return "\n".join(lines) + "\n"


def threshold_table() -> str:
    lines = ["| metric | better | fails when worse by more than | rationale |", "| --- | --- | --- | --- |"]
    for n, m in METRICS.items():
        floor = f" and {m.min_abs_change:g} {m.unit}" if m.min_abs_change else ""
        lines.append(f"| `{n}` | {m.better} | {m.max_regression_pct:g}%{floor} | {m.rationale} |")
    return "\n".join(lines) + "\n"


def main(argv: list[str] | None = None) -> int:
    import argparse
    p = argparse.ArgumentParser(prog="python3 -m aerie_bench.perf", description="Perf documents and reports.")
    sub = p.add_subparsers(dest="cmd", required=True)
    c = sub.add_parser("compare", help="judge CURRENT against BASELINE; exit 1 on a regression")
    c.add_argument("current"), c.add_argument("baseline")
    s = sub.add_parser("series", help="Markdown table of every recorded build in DIR")
    s.add_argument("dir")
    b = sub.add_parser("side-by-side", help="Markdown table of an Aerie and another browser's perf.json")
    b.add_argument("aerie"), b.add_argument("other"), b.add_argument("--other-name", default="Helium")
    args = p.parse_args(argv)
    try:
        if args.cmd == "compare":
            v = compare(load(Path(args.current)), load(Path(args.baseline)))
            print("\n".join(verdict_lines(v)))
            return 1 if failures(v) else 0
        if args.cmd == "series":
            print(series_report(read_history(Path(args.dir))), end="")
        else:
            print(side_by_side(load(Path(args.aerie)), load(Path(args.other)), args.other_name), end="")
    except (OSError, ValueError) as exc:
        print(f"aerie_bench.perf: {exc}")
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
