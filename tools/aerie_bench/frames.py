"""Sidebar animation frame times (issue #195): read a Chromium trace, measure, judge, compare.

Pure logic, no browser. The ``sidebar_frames`` check (checks/sidebar_frames.py) observes: it drives the
native sidebar and records a trace. This module judges.

A trace is a list of Chrome trace events (the CDP ``Tracing.dataCollected`` values). ``condense`` keeps
the four things that matter, as plain lists of microseconds on the trace clock (``CLOCK_MONOTONIC`` on
Linux, the same clock as Python's ``time.monotonic``):

``pipeline``  one entry per compositor frame the browser began: ``[begin_us, duration_us, state]``. They come from
              the ``PipelineReporter`` async events (category ``cc,benchmark,disabled-by-default-devtools.timeline.frame``).
              ``duration_us`` runs from the begin-frame to the end of the pipeline; ``state`` is the reporter's own
              verdict (``STATE_PRESENTED_ALL``, ``STATE_PRESENTED_PARTIALLY``, ``STATE_DROPPED`` or
              ``STATE_NO_UPDATE_DESIRED``). The browser process and the viz process each report the same frame
              (same ``frame_sequence``); the worse state and the longer duration win.
``draws``     the time of every ``STEP_DRAW_AND_SWAP`` of ``Graphics.Pipeline``: a frame reached the screen.
``layouts``   the time of every ``View::LayoutImmediately``: a Views layout ran.
``steps``     the time of every ``LayerAnimator::Step``: a layer animation ticked.

``measure`` turns one animation's window into numbers. ``judge`` and ``compare`` decide. Thresholds and the
reasons for them are in ``ABSOLUTE`` and ``METRICS``; docs/bench.md prints the same tables.
"""

from __future__ import annotations

import json
import math
import os
from dataclasses import dataclass
from pathlib import Path

from . import perf

SCHEMA = 1
HISTORY_FILE = "sidebar-frames-history.jsonl"
MAIN_FILE = "sidebar-frames-main.json"

REFRESH_HZ = 60
VSYNC_MS = 1000.0 / REFRESH_HZ
# A gap between two frames on screen counts as missed frames from 1.5 vsyncs up: a frame that is a
# millisecond late is jitter, a frame that is a whole vsync late is a drop.
MISSED_GAP_VSYNCS = 1.5
# A layout this close to the first or last frame of an animation is the start or the end of it (the
# card snapping, the column leaving the layout), not a layout per frame.
EDGE_VSYNCS = 1.5

CATEGORIES = ("benchmark", "viz", "graphics.pipeline", "disabled-by-default-devtools.timeline.frame",
              "ui", "views")

PRESENTED = ("STATE_PRESENTED_ALL", "STATE_PRESENTED_PARTIALLY")
_STATE_RANK = {"STATE_NO_UPDATE_DESIRED": 0, "STATE_PRESENTED_ALL": 1, "STATE_PRESENTED_PARTIALLY": 2,
               "STATE_DROPPED": 3}


# --- reading a trace ------------------------------------------------------------------------------

def condense(events: list[dict]) -> dict:
    """The parts of a trace that ``measure`` reads. Plain data, small enough to keep as a test fixture."""
    begun: dict[tuple, dict] = {}
    frames: dict[int, list] = {}
    draws: list[int] = []
    layouts: list[int] = []
    steps: list[int] = []
    for ev in sorted((e for e in events if "ts" in e), key=lambda e: e["ts"]):
        name, ph = ev.get("name"), ev.get("ph")
        ts = int(ev["ts"])
        if name == "PipelineReporter":
            key = (ev.get("pid"), (ev.get("id2") or {}).get("local", ev.get("id")))
            if ph == "b":
                begun[key] = ev
            elif ph == "e" and key in begun:
                start = begun.pop(key)
                report = (start.get("args") or {}).get("frame_reporter") or {}
                state = report.get("state")
                seq = report.get("frame_sequence")
                if state is None or seq is None:
                    continue
                entry = [int(start["ts"]), ts - int(start["ts"]), state]
                old = frames.get(seq)
                if old is None:
                    frames[seq] = entry
                else:
                    worse = max(old[2], entry[2], key=lambda s: _STATE_RANK.get(s, 0))
                    frames[seq] = [min(old[0], entry[0]), max(old[1], entry[1]), worse]
        elif name == "Graphics.Pipeline" and ph == "X":
            step = ((ev.get("args") or {}).get("chrome_graphics_pipeline") or {}).get("step")
            if step == "STEP_DRAW_AND_SWAP":
                draws.append(ts)
        elif name == "View::LayoutImmediately" and ph == "X":
            layouts.append(ts)
        elif name == "LayerAnimator::Step":
            steps.append(ts)
    return {"pipeline": sorted(frames.values()), "draws": draws, "layouts": layouts, "steps": steps}


def percentile(values: list[float], pct: float) -> float | None:
    """Nearest rank: the smallest value that at least ``pct`` percent of the values do not exceed."""
    if not values:
        return None
    ordered = sorted(values)
    rank = max(1, math.ceil(pct / 100.0 * len(ordered)))
    return ordered[min(rank, len(ordered)) - 1]


def _r(value: float | None, digits: int = 1) -> float | None:
    return None if value is None else round(value, digits)


# --- one animation ---------------------------------------------------------------------------------

def measure(trace: dict, start_us: int, duration_ms: float, vsync_ms: float = VSYNC_MS) -> dict:
    """Numbers for one animation that began at ``start_us`` and should end ``duration_ms`` later.

    The window runs from ``start_us`` to the nominal end plus one vsync. The first frame on screen is the
    first draw in the window; the time from ``start_us`` to it is input and scheduling latency and is not
    held against the animation.

    - ``frames``: frames the compositor presented inside the window.
    - ``frame_time_ms``: p50, p95, p99 of those frames' pipeline duration (begin-frame to presented).
    - ``interval_ms``: p50, p95, p99 of the gaps between draws.
    - ``missed_frames``: vsyncs with no frame on screen, from the gaps (``MISSED_GAP_VSYNCS``), counting a
      stall between the last draw and the nominal end too.
    - ``dropped_reports``: frames the compositor itself reported as ``STATE_DROPPED`` in the window.
    - ``dropped_frames``: the larger of the two counts above (a dropped frame leaves a gap, so adding
      them would count it twice).
    - ``layout_frames``: vsync slots between the first and the last frame, edges excluded, in which a Views
      layout ran. ``compositor_only`` is true when there are none.
    """
    vsync_us = vsync_ms * 1000.0
    end_us = start_us + duration_ms * 1000.0
    window_end = end_us + vsync_us
    draws = sorted(t for t in trace["draws"] if start_us <= t <= window_end)
    pipeline = [p for p in trace["pipeline"] if start_us <= p[0] <= window_end]
    presented = [p for p in pipeline if p[2] in PRESENTED]
    times = [p[1] / 1000.0 for p in presented]
    gaps = [(b - a) / 1000.0 for a, b in zip(draws, draws[1:])]
    missed = sum(round(g / vsync_ms) - 1 for g in gaps if g > MISSED_GAP_VSYNCS * vsync_ms)
    if draws:
        tail = (end_us - draws[-1]) / 1000.0
        if tail > MISSED_GAP_VSYNCS * vsync_ms:
            missed += round(tail / vsync_ms) - 1
    else:
        missed = max(1, round(duration_ms / vsync_ms))  # nothing reached the screen at all
    dropped_reports = sum(1 for p in pipeline if p[2] == "STATE_DROPPED")
    layout_slots: set[int] = set()
    if len(draws) >= 2:
        lo = draws[0] + EDGE_VSYNCS * vsync_us
        hi = draws[-1] - EDGE_VSYNCS * vsync_us
        for t in trace["layouts"]:
            if lo < t < hi:
                layout_slots.add(int((t - draws[0]) // vsync_us))
    return {
        "frames": len(presented),
        "draws": len(draws),
        "frame_time_ms": {"p50": _r(percentile(times, 50)), "p95": _r(percentile(times, 95)),
                          "p99": _r(percentile(times, 99)), "max": _r(max(times) if times else None)},
        "interval_ms": {"p50": _r(percentile(gaps, 50)), "p95": _r(percentile(gaps, 95)),
                        "p99": _r(percentile(gaps, 99)), "max": _r(max(gaps) if gaps else None)},
        "missed_frames": missed,
        "dropped_reports": dropped_reports,
        "dropped_frames": max(missed, dropped_reports),
        "layout_frames": len(layout_slots),
        "compositor_only": not layout_slots,
        "span_ms": _r((draws[-1] - draws[0]) / 1000.0) if len(draws) >= 2 else 0.0,
    }


def pool(runs: list[dict]) -> dict:
    """One animation measured several times, as one result. Each run carries its raw values in ``_times`` (the
    frame times) and ``_gaps`` (the intervals between frames on screen); the summary is taken over those of
    every run."""
    times = [t for r in runs for t in r.get("_times", [])]
    gaps = [g for r in runs for g in r.get("_gaps", [])]
    out = {
        "runs": len(runs),
        "frames": sum(r["frames"] for r in runs),
        "frame_time_ms": {"p50": _r(percentile(times, 50)), "p95": _r(percentile(times, 95)),
                          "p99": _r(percentile(times, 99)), "max": _r(max(times) if times else None)},
        "interval_ms": {"p50": _r(percentile(gaps, 50)), "p95": _r(percentile(gaps, 95)),
                        "p99": _r(percentile(gaps, 99)), "max": _r(max(gaps) if gaps else None)},
        "missed_frames": sum(r["missed_frames"] for r in runs),
        "dropped_reports": sum(r["dropped_reports"] for r in runs),
        "dropped_frames": sum(r["dropped_frames"] for r in runs),
        "dropped_per_run": [r["dropped_frames"] for r in runs],
        "layout_frames": sum(r["layout_frames"] for r in runs),
        "compositor_only": all(r["compositor_only"] for r in runs),
    }
    return out


def measure_runs(trace: dict, starts_us: list[int], duration_ms: float, vsync_ms: float = VSYNC_MS) -> dict:
    """``measure`` for each start, pooled: percentiles over all frames of all runs, counts summed."""
    runs = []
    for start in starts_us:
        run = measure(trace, start, duration_ms, vsync_ms)
        end = start + duration_ms * 1000.0 + vsync_ms * 1000.0
        run["_times"] = [p[1] / 1000.0 for p in trace["pipeline"]
                         if start <= p[0] <= end and p[2] in PRESENTED]
        draws = sorted(t for t in trace["draws"] if start <= t <= end)
        run["_gaps"] = [(b - a) / 1000.0 for a, b in zip(draws, draws[1:])]
        runs.append(run)
    out = pool(runs)
    out["starts_us"] = list(starts_us)  # on the trace clock, to find the animation in the saved trace
    return out


# --- judging ---------------------------------------------------------------------------------------

@dataclass(frozen=True)
class Limit:
    what: str
    rationale: str


# The acceptance limits of issue #195, at 60 Hz. Each applies to every animation of the run, normal motion
# and reduced motion. ``compositor_only`` applies where the design allows no layout per frame (docs/sidebar-design.md,
# "Motion", rule 2): the column slide and the reduced-motion crossfade.
ABSOLUTE: dict[str, Limit] = {
    "p95_frame_time_ms": Limit(
        f"at most {VSYNC_MS:.1f} ms",
        "The 95th percentile of the time from a frame's begin-frame to the frame on screen. One vsync at 60 Hz is the "
        "budget: a frame that needs longer than that is on screen a vsync late."),
    "dropped_frames": Limit(
        "0",
        "Vsyncs with no new frame while an animation runs, counted from gaps between frames on screen (1.5 vsyncs or "
        "more) and from the compositor's own `STATE_DROPPED` reports, whichever is larger. The issue allows none."),
    "compositor_only": Limit(
        "true where the design allows no layout per frame",
        "No Views layout between the first and last frame of the slide or the crossfade. A layout per frame means "
        "something animates a size or a position and not a layer's transform or opacity."),
}
P95_LIMIT_MS = round(VSYNC_MS, 1)

# The animations of a run, in the order they are driven. ``duration_ms`` is the longest the design lets it run
# (docs/sidebar-design.md, motion tokens). ``layout_free`` is whether ``compositor_only`` is judged for it.
ANIMATIONS: dict[str, dict] = {
    "show": {"duration_ms": 280, "layout_free": True, "reduced_ms": 140},
    "hide": {"duration_ms": 200, "layout_free": True, "reduced_ms": 140},
    "folder_expand": {"duration_ms": 200, "layout_free": False, "reduced_ms": 90},
    "folder_collapse": {"duration_ms": 180, "layout_free": False, "reduced_ms": 90},
    "resize": {"duration_ms": 400, "layout_free": False, "reduced_ms": 400},
}


def duration_ms(name: str) -> int:
    """The nominal length of an animation by its measured name (``reduced_show`` is the 140 ms crossfade)."""
    base = name.removeprefix("reduced_")
    return ANIMATIONS[base]["reduced_ms" if name.startswith("reduced_") else "duration_ms"]


def animation_names() -> list[str]:
    """Every measured animation: the five, then the four that have a reduced-motion form."""
    return list(ANIMATIONS) + [f"reduced_{n}" for n in ANIMATIONS if n != "resize"]


def judge(observation: dict, names: tuple[str, ...] | None = None) -> list[str]:
    """The problems with an observation (see the check), against the acceptance limits. Empty: it passes.
    ``names`` limits the judgement to some animations (the negative control only runs the folder)."""
    problems: list[str] = []
    anims = observation.get("animations") or {}
    if not anims:
        return ["no animation was measured"]
    for name in (names or animation_names()):
        spec = ANIMATIONS[name.removeprefix("reduced_")]
        m = anims.get(name)
        if m is None:
            problems.append(f"{name}: not measured")
            continue
        if not m.get("frames"):
            problems.append(f"{name}: no frame was presented")
            continue
        p95 = (m.get("frame_time_ms") or {}).get("p95")
        if p95 is None or p95 > P95_LIMIT_MS:
            problems.append(f"{name}: p95 frame time {p95} ms, limit {P95_LIMIT_MS} ms")
        if m.get("dropped_frames", 0) > 0:
            problems.append(f"{name}: {m['dropped_frames']} dropped frames "
                            f"({m.get('missed_frames', 0)} missed vsyncs, {m.get('dropped_reports', 0)} reported dropped; "
                            f"per run {m.get('dropped_per_run')})")
        if spec["layout_free"] and not m.get("compositor_only", False):
            problems.append(f"{name}: layout ran in {m.get('layout_frames')} frames of the animation; only layers "
                            "may animate")
    return problems


NOT_RUN = ("not measured", "no frame was presented", "no animation was measured")


def control_verdict(problems: list[str]) -> str:
    """How the negative control went, from what ``judge`` said about it.

    ``"failed"``: it dropped frames or was too slow, as a layout-heavy path must. ``"broken"``: something did not run
    (an animation not measured, no frame presented), so the control proves nothing. ``"passed"``: it ran and met
    the limits, so the judge cannot tell a bad animation from a good one."""
    if any(any(marker in p for marker in NOT_RUN) for p in problems):
        return "broken"
    if any("p95 frame time" in p or "dropped frames" in p for p in problems):
        return "failed"
    return "passed"


# --- baseline --------------------------------------------------------------------------------------

# Regression against the last recorded main build. The metric name in a document is ``<animation>.<kind>``.
METRICS: dict[str, perf.Metric] = {
    "p95_ms": perf.Metric(
        "ms", "lower", 25.0, 2.0, True,
        "The 95th percentile frame time of one animation. Frame time is a few ms in the steady state and about one "
        "vsync for the first frame of a slide, and a run of a few repeats moves by a millisecond or two on its own, so a "
        "change needs both 25% and 2 ms."),
    "dropped_frames": perf.Metric(
        "frames", "lower", 0.0, 0.0, True,
        "Dropped frames, summed over the repeats of one animation. Any increase over the baseline fails: the "
        "acceptance limit is zero, so a baseline above zero can only go down."),
}


def metric_name(animation: str, kind: str) -> str:
    return f"{animation}.{kind}"


def flat_metrics(animations: dict) -> dict[str, float]:
    out: dict[str, float] = {}
    for name, m in animations.items():
        p95 = (m.get("frame_time_ms") or {}).get("p95")
        if p95 is not None:
            out[metric_name(name, "p95_ms")] = p95
        out[metric_name(name, "dropped_frames")] = m.get("dropped_frames", 0)
    return out


def document(animations: dict, *, binary: dict | None = None, started: str | None = None, label: str | None = None,
             control: dict | None = None) -> dict:
    doc = {"schema": SCHEMA, "label": label, "binary": binary or {}, "started": started,
           "refresh_hz": REFRESH_HZ, "metrics": flat_metrics(animations), "animations": animations}
    if control is not None:
        doc["control"] = control
    return doc


def compare(current: dict, baseline: dict) -> list[dict]:
    """One verdict per metric of either document: ``ok``, ``regressed``, ``improved``, ``new`` (no baseline
    value) or ``missing`` (the baseline has it, this run does not: an animation went unmeasured)."""
    cur, base = current.get("metrics", {}), baseline.get("metrics", {})
    out = []
    for name in sorted(set(cur) | set(base)):
        kind = name.rsplit(".", 1)[-1]
        spec = METRICS.get(kind)
        c, b = cur.get(name), base.get(name)
        row = {"metric": name, "current": c, "baseline": b, "change_pct": None, "status": "ok"}
        if c is None:
            row["status"] = "missing"
        elif b is None:
            row["status"] = "new"
        else:
            row["change_pct"] = round((c - b) / abs(b) * 100.0, 1) if b else None
            if spec and perf.regressed(spec, c, b):
                row["status"] = "regressed"
            elif spec and perf.regressed(spec, b, c):
                row["status"] = "improved"
        out.append(row)
    return out


def failures(verdicts: list[dict]) -> list[dict]:
    return [v for v in verdicts if v["status"] in ("regressed", "missing")]


def verdict_lines(verdicts: list[dict]) -> list[str]:
    lines = []
    for v in verdicts:
        spec = METRICS.get(v["metric"].rsplit(".", 1)[-1])
        change = "" if v["change_pct"] is None else f" ({v['change_pct']:+.1f}%)"
        limit = ""
        if spec:
            floor = f" and {spec.min_abs_change:g} {spec.unit}" if spec.min_abs_change else ""
            limit = f", limit {spec.max_regression_pct:g}%{floor}"
        cur = "-" if v["current"] is None else f"{v['current']:g}"
        base = "-" if v["baseline"] is None else f"{v['baseline']:g}"
        lines.append(f"{v['status'].upper():10} {v['metric']}: {cur} against {base}{change}{limit}")
    return lines


def threshold_table() -> str:
    lines = ["| metric | better | fails when worse by more than | rationale |", "| --- | --- | --- | --- |"]
    for kind, m in METRICS.items():
        floor = f" and {m.min_abs_change:g} {m.unit}" if m.min_abs_change else ""
        lines.append(f"| `<animation>.{kind}` | {m.better} | {m.max_regression_pct:g}%{floor} | {m.rationale} |")
    return "\n".join(lines) + "\n"


def absolute_table() -> str:
    lines = ["| measure | limit | meaning |", "| --- | --- | --- |"]
    for name, lim in ABSOLUTE.items():
        lines.append(f"| `{name}` | {lim.what} | {lim.rationale} |")
    return "\n".join(lines) + "\n"


# --- documents on disk -----------------------------------------------------------------------------

def load(path: Path) -> dict:
    doc = json.loads(Path(path).read_text())
    if not isinstance(doc, dict) or doc.get("schema") != SCHEMA or not isinstance(doc.get("metrics"), dict):
        raise ValueError(f"{path}: not a schema {SCHEMA} sidebar frames document")
    return doc


def record(directory: Path, doc: dict) -> Path:
    """Append ``doc`` to the series and make it the baseline for the next comparison."""
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    with open(directory / HISTORY_FILE, "a") as f:
        f.write(json.dumps(doc, sort_keys=True) + "\n")
    tmp = directory / (MAIN_FILE + ".tmp")
    tmp.write_text(json.dumps(doc, indent=1, sort_keys=True) + "\n")
    os.replace(tmp, directory / MAIN_FILE)
    return directory / MAIN_FILE
