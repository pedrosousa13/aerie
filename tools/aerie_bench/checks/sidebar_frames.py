"""sidebar_frames: frame times of the native sidebar's animations (issue #195).

The check starts a browser with a real window on a private Xvfb (60 Hz, no window manager), seeds a profile with an
auto-filing folder and opens tabs that the folder takes, and then drives the sidebar the way a user does, with real
X events: Ctrl+S to hide and show it, a mouse drag on its edge to resize it, a click on the folder row to expand and
collapse it. A Chromium trace (CDP ``Tracing``, the browser's own compositor and viz frame events) is recorded
around each animation. ``frames.py`` turns it into numbers and judges them: p50, p95, p99 of the frame time, the
frames that were dropped in each animation, and whether only layers animated.

Observe and judge are apart: ``Observer`` drives and records, ``frames.judge`` decides, and the judge has unit tests on
recorded traces (tests/test_bench_frames.py, tools/aerie_bench/testdata/sidebar_frames).

A second browser runs the same with ``--force-prefers-reduced-motion`` (the crossfades). A third browser is the
negative control: a folder that holds 300 tabs, so that expanding it lays out hundreds of rows in the animation. The
judge must fail it. If it does not, the check fails, because a judge that passes that proves nothing.

With a baseline (``--sidebar-frames-baseline``) the check also fails when an animation got worse than the last main
build by more than the thresholds in docs/bench.md. Every page it loads is on ``*.localhost``; no trace leaves the
machine. Not in the default scenario.
"""

from __future__ import annotations

import json
import time
from datetime import datetime, timezone
from pathlib import Path

from .. import frames, ui_devtools, xinput
from ..session import CheckResult, Session
from . import register

WINDOW_W, WINDOW_H = 1200, 760
FOLDER_HOST = "folder.localhost"      # the auto-filing rule: this host and its subdomains
TAB_PAGE = "/sidebar_frames/tab.html"
WORKSPACE_ID = "personal"
FOLDER_ID = "frames-folder"
RULE_ID = "frames-rule"
TOAST_SECONDS = 8.5                   # the "Filed into ..." toast holds 6 s and then fades; wait it out
WINDOWS_EPOCH_OFFSET_US = 11_644_473_600 * 1_000_000
RESIZE_STEPS = 24                     # one pointer move per vsync, about 400 ms
RESIZE_DELTA = 100                    # px the edge moves out (260 to 360, inside the 200 to 400 limits)

UI_FLAGS = (*ui_devtools.FLAGS, f"--window-size={WINDOW_W},{WINDOW_H}", "--window-position=0,0",
            "--ozone-platform=x11")
REDUCED_FLAG = "--force-prefers-reduced-motion"
CONTROL_ANIMATIONS = ("folder_expand", "folder_collapse")  # judged in the negative control
ALL_GROUPS = ("hide_show", "folder", "resize")


def state_json(now: float | None = None) -> dict:
    """``Default/AerieTabModel/state.json`` for a profile with one auto-filing folder in Today (collapsed) that files
    every tab opened on ``folder.localhost``. Schema 1 (components/aerie_tab_model/tab_model_types.cc); times are
    microseconds since 1601, as strings."""
    stamp = str(int((now if now is not None else time.time()) * 1_000_000) + WINDOWS_EPOCH_OFFSET_US)
    return {
        "version": 1, "next_change": "10", "changes": [], "tabs": [],
        "workspace": {"change": "3", "colour": "dusk", "created": stamp, "icon": "ring", "id": WORKSPACE_ID,
                      "mode": "default", "modified": stamp, "name": "Personal", "theme": "default"},
        "folders": [{"change": "1", "created": stamp, "modified": stamp, "id": FOLDER_ID,
                     "workspace_id": WORKSPACE_ID, "section": "today", "parent": "", "position": 0, "name": "Bench",
                     "icon": "folder", "kind": "auto", "expanded": False,
                     "rules": {"enabled": True, "rules": [{"id": RULE_ID, "type": "domain", "value": FOLDER_HOST,
                                                            "enabled": True}]}}],
    }


def seed_profile(profile_path: Path, now: float | None = None) -> Path:
    target = Path(profile_path) / "Default" / "AerieTabModel" / "state.json"
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(json.dumps(state_json(now=now)))
    return target


def tab_urls(base_url: str, count: int) -> list[str]:
    """``count`` addresses on ``*.folder.localhost`` that the folder rule takes (the fixture server answers any
    ``*.localhost`` name with the loopback fixtures)."""
    port = base_url.rsplit(":", 1)[1]
    return [f"http://t{i}.{FOLDER_HOST}:{port}{TAB_PAGE}?n={i}" for i in range(count)]


now_us = xinput.now_us


class InputLost(RuntimeError):
    """A key or click did not change the sidebar, so the animation that was timed never ran."""


class Observer:
    """Drives one browser through the animations and records a trace around each. Plain recording: it decides nothing."""

    def __init__(self, browser: Session, ui: ui_devtools.UiDevTools, xin: xinput.XInput, *, repeats: int,
                 read_expanded: bool = True) -> None:
        self.browser, self.ui, self.xin, self.repeats = browser, ui, xin, repeats
        self.read_expanded = read_expanded
        self.traces: dict[str, dict] = {}

    # -- recording

    def record(self, label: str, action) -> dict:
        """Trace ``action()``; return the condensed trace. The action returns what it wants to keep."""
        client = self.browser.client
        client.take_events("Tracing.dataCollected")
        client.drain("Tracing.tracingComplete")
        client.send("Tracing.start", {"traceConfig": {"recordMode": "recordAsMuchAsPossible",
                                                      "includedCategories": list(frames.CATEGORIES)},
                                      "transferMode": "ReportEvents"}, timeout=self.browser.timeout)
        time.sleep(0.4)
        try:
            result = action()
        finally:
            client.send("Tracing.end", timeout=self.browser.timeout)
            client.wait_event("Tracing.tracingComplete", timeout=90)
        events = [e for msg in client.take_events("Tracing.dataCollected") for e in msg["params"]["value"]]
        trace = frames.condense(events)
        self.traces[label] = trace
        return {"trace": trace, "result": result}

    # -- the animations

    def _pair(self, label: str, first: str, second: str, act_first, act_second, expect_first, expect_second
              ) -> dict[str, dict]:
        """Alternate two actions ``repeats`` times in one trace; measure each action's animation.

        An action returns the trace-clock time of its first state-changing input event (the key going down, the
        button press); the window starts there, not before the pointer or the keys were moved into place.
        ``expect_*`` are (description, callable): after the animation the callable must be true, or the input
        was lost and the run fails."""
        starts: dict[str, list[int]] = {first: [], second: []}

        def run():
            for _ in range(self.repeats):
                for name, act, expect in ((first, act_first, expect_first), (second, act_second, expect_second)):
                    before = now_us()
                    pressed = act()
                    starts[name].append(pressed if pressed is not None else before)
                    time.sleep(frames.duration_ms(name) / 1000.0 + 0.45)
                    self.expect(name, *expect)

        got = self.record(label, run)
        return {name: frames.measure_runs(got["trace"], starts[name], frames.duration_ms(name))
                for name in (first, second)}

    def expect(self, name: str, description: str, check) -> None:
        """Fail the run, clearly, when the input of ``name`` did not take effect."""
        if check is not None and not check():
            raise InputLost(f"{name}: the input was lost, {description} (the sidebar did not change; the animation "
                            "that was timed never ran)")

    def sidebar_open(self) -> bool:
        rect = self.ui.screen_rect("VerticalTabStripRegionView")
        return rect is not None and rect[2] > 0

    def folder_rows(self) -> int:
        """Tab rows in the views tree. Counted from the tree alone: asking for the properties of 300 rows is slow."""
        return sum(1 for _, cls in self.ui.nodes_within("AerieSidebarView") if cls == "AerieSidebarRowView")

    def folder_expanded(self) -> bool:
        return self.folder_rows() > self.collapsed_rows

    # The expanded folder of the control (300 tabs) cannot be read: the views tree of 300 rows does not come back from
    # UI DevTools within the time limit. Its expand is then not read; the collapse is, and since expand and collapse
    # alternate, a lost expand leaves the folder expanded after the collapse click, which fails that check.

    def hide_show(self, reduced: bool) -> dict[str, dict]:
        key = (lambda n: f"reduced_{n}" if reduced else n)
        toggle = lambda: self.xin.chord("ctrl", "s")
        return self._pair("hide_show" + ("_reduced" if reduced else ""), key("hide"), key("show"), toggle, toggle,
                          ("the sidebar is still visible after Ctrl+S", lambda: not self.sidebar_open()),
                          ("the sidebar is still hidden after Ctrl+S", self.sidebar_open))

    def folder(self, reduced: bool) -> dict[str, dict]:
        x, y = self.folder_point
        click = lambda: self.xin.click(x, y)
        key = (lambda n: f"reduced_{n}" if reduced else n)
        return self._pair("folder" + ("_reduced" if reduced else ""), key("folder_expand"), key("folder_collapse"),
                          click, click,
                          ("the folder is still collapsed after the click", self.folder_expanded if self.read_expanded else None),
                          ("the folder is still expanded after the click", lambda: not self.folder_expanded()))

    def resize(self) -> dict[str, dict]:
        rect = self.ui.screen_rect("ResizeArea", within="VerticalTabStripRegionView")
        if rect is None:
            raise RuntimeError("the sidebar's resize area is not in the views tree")
        x, y = rect[0] + rect[2] // 2, WINDOW_H // 2
        step = frames.VSYNC_MS / 1000.0
        starts: list[int] = []

        def run():
            for _ in range(max(1, self.repeats // 2 + 1)):
                for sign in (1, -1):
                    start_x = x if sign == 1 else x + RESIZE_DELTA
                    starts.append(self.xin.drag(start_x, y, start_x + sign * RESIZE_DELTA, y, RESIZE_STEPS, step))
                    time.sleep(0.5)

        got = self.record("resize", run)
        return {"resize": frames.measure_runs(got["trace"], starts, frames.duration_ms("resize"))}

    # -- set-up

    def locate(self) -> None:
        folder = self.ui.screen_rect("AerieSidebarFolderRowView", within="AerieSidebarView")
        if folder is None:
            raise RuntimeError("the sidebar shows no folder row: the auto-filing folder was not loaded")
        self.folder_point = (folder[0] + folder[2] // 2, folder[1] + folder[3] // 2)
        self.collapsed_rows = self.folder_rows()  # the folder starts collapsed: more rows than this means expanded

    def warm_up(self) -> None:
        """One of each, untraced: the first run of a view's layer, a shader or a glyph cache is not what the user feels
        every time."""
        self.xin.chord("ctrl", "s")
        time.sleep(0.8)
        self.expect("warm-up hide", "the sidebar is still visible after Ctrl+S", lambda: not self.sidebar_open())
        self.xin.chord("ctrl", "s")
        time.sleep(0.8)
        self.expect("warm-up show", "the sidebar is still hidden after Ctrl+S", self.sidebar_open)
        self.xin.click(*self.folder_point)
        time.sleep(0.8)
        self.expect("warm-up expand", "the folder is still collapsed after the click",
                    self.folder_expanded if self.read_expanded else None)
        self.xin.click(*self.folder_point)
        time.sleep(0.8)
        self.expect("warm-up collapse", "the folder is still expanded after the click",
                    lambda: not self.folder_expanded())


@register
class SidebarFrames:
    name = "sidebar_frames"
    seeds_helium_services = False

    def __init__(self, baseline: str | None = None, first_run: bool = False, repeats: int = 5, tabs: int = 15,
                 control_tabs: int = 300, label: str | None = None, observer_factory=None) -> None:
        self.baseline = baseline
        self.first_run = first_run
        self.repeats = max(2, repeats)
        self.tabs = tabs
        self.control_tabs = control_tabs
        self.label = label
        self.observe = observer_factory or self.observe_browser

    # -- observation

    def observe_browser(self, session: Session, display: str, *, reduced: bool, tabs: int,
                        groups: tuple[str, ...] = ALL_GROUPS) -> tuple[dict[str, dict], dict]:
        """One browser, one pass. Returns the measured animations and the condensed traces."""
        flags = (*UI_FLAGS, *((REDUCED_FLAG,) if reduced else ()))
        env = {**xinput.display_env(display), "WAYLAND_DISPLAY": ""}
        with session.new_profile() as profile:
            seed_profile(profile.path)
            with session.variant(extra_flags=flags, profile=profile, headed=True, env=env) as browser:
                for url in tab_urls(browser.fixtures.base_url, tabs):
                    browser.client.send("Target.createTarget", {"url": url, "background": True},
                                        timeout=browser.timeout)
                time.sleep(TOAST_SECONDS + (tabs / 40.0 if tabs > 50 else 0))
                ui = ui_devtools.connect(profile.path, timeout=browser.timeout)
                xin = xinput.XInput(display)
                try:
                    xin.move(WINDOW_W // 2, WINDOW_H // 2)  # no window manager: the key focus follows the pointer
                    time.sleep(0.5)
                    obs = Observer(browser, ui, xin, repeats=self.repeats, read_expanded=tabs <= 50)
                    obs.locate()
                    obs.warm_up()
                    measured: dict[str, dict] = {}
                    if "hide_show" in groups:
                        measured.update(obs.hide_show(reduced))
                    if "folder" in groups:
                        measured.update(obs.folder(reduced))
                    if "resize" in groups and not reduced:  # there is no reduced form of dragging the edge
                        measured.update(obs.resize())
                finally:
                    xin.close()
                    ui.close()
                return measured, obs.traces

    # -- the check

    def run(self, session: Session) -> CheckResult:
        missing = xinput.have_tools()
        if missing:
            return CheckResult(self.name, "fail", f"cannot drive a window here: {missing} missing "
                               "(the check needs Xvfb and the X11 and Xtst libraries)")
        started = datetime.now(timezone.utc).isoformat(timespec="seconds")
        traces: dict[str, dict] = {}
        with xinput.xvfb(WINDOW_W + 80, WINDOW_H + 40) as display:
            animations, tr = self.observe(session, display, reduced=False, tabs=self.tabs)
            traces.update({f"normal.{k}": v for k, v in tr.items()})
            reduced, tr = self.observe(session, display, reduced=True, tabs=self.tabs)
            traces.update({f"reduced.{k}": v for k, v in tr.items()})
            animations.update(reduced)
            control, tr = self.observe(session, display, reduced=False, tabs=self.control_tabs, groups=("folder",))
            traces.update({f"control.{k}": v for k, v in tr.items()})
        observation = {"animations": animations}
        problems = frames.judge(observation)
        control_problems = frames.judge({"animations": control}, names=CONTROL_ANIMATIONS)
        doc = frames.document(animations, binary={"path": session.binary, "version": session.binary_version},
                              started=started, label=self.label, control=control)
        (session.out_dir / "sidebar_frames.json").write_text(json.dumps(doc, indent=1) + "\n")
        (session.out_dir / "sidebar_frames_traces.json").write_text(json.dumps(traces, separators=(",", ":")))
        session.artifacts += ["sidebar_frames.json", "sidebar_frames_traces.json"]
        data = {"animations": animations, "control": control, "problems": problems,
                "control_problems": control_problems, "metrics": doc["metrics"]}
        summary = summarise(animations)
        verdict = frames.control_verdict(control_problems)
        data["control_verdict"] = verdict
        if verdict == "broken":
            return CheckResult(self.name, "fail", "the negative control did not run: " + "; ".join(control_problems)
                               + ". The harness measured nothing for a folder of "
                               f"{self.control_tabs} tabs, so the judge was not tried. " + summary, data=data)
        if verdict == "passed":
            return CheckResult(self.name, "fail", "the negative control did not fail: a folder of "
                               f"{self.control_tabs} tabs expanded and collapsed within the limits, so the judge cannot "
                               "be trusted. " + summary, data=data)
        if problems:
            return CheckResult(self.name, "fail", "; ".join(problems) + ". " + summary + ". Negative control (a folder of "
                               f"{self.control_tabs} tabs) failed as it must, with {len(control_problems)} problems",
                               data=data)
        return self.compare_baseline(doc, summary, data)

    def compare_baseline(self, doc: dict, summary: str, data: dict) -> CheckResult:
        control_note = f"negative control failed as it must ({len(data['control_problems'])} problems)"
        if not self.baseline:
            return CheckResult(self.name, "pass", f"{summary}; {control_note}; no baseline given, nothing compared",
                               data=data)
        try:
            base = frames.load(Path(self.baseline))
        except FileNotFoundError:
            if self.first_run:
                return CheckResult(self.name, "pass", f"{summary}; {control_note}; first run, no baseline at "
                                   f"{self.baseline} (--sidebar-frames-first-run)", data=data)
            return CheckResult(self.name, "fail", f"{summary}; no baseline at {self.baseline}, so nothing was compared. "
                               "Record one from a main build with --sidebar-frames-record DIR, or pass "
                               "--sidebar-frames-first-run for the very first run", data=data)
        except (OSError, ValueError) as exc:
            return CheckResult(self.name, "error", f"cannot read the baseline: {exc}", data=data)
        verdicts = frames.compare(doc, base)
        data["verdicts"] = verdicts
        bad = frames.failures(verdicts)
        if bad:
            return CheckResult(self.name, "fail", "regressed past threshold: " + "; ".join(frames.verdict_lines(bad)),
                               data=data)
        return CheckResult(self.name, "pass", f"{summary}; {control_note}; within thresholds of the baseline", data=data)


def summarise(animations: dict) -> str:
    parts = []
    for name in frames.animation_names():
        m = animations.get(name)
        if not m:
            continue
        ft = m.get("frame_time_ms") or {}
        parts.append(f"{name}: p50 {ft.get('p50')} p95 {ft.get('p95')} p99 {ft.get('p99')} ms, "
                     f"{m.get('dropped_frames')} dropped, {'layers only' if m.get('compositor_only') else 'layout ran'}")
    return "; ".join(parts)
