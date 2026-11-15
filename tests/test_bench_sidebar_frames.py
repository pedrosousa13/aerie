"""The sidebar_frames check (issue #195): the seed, the verdicts, the negative control, the baseline and the flags."""

import contextlib
import io
import json
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

from aerie_bench import cli, frames, xinput
from aerie_bench.checks import REGISTRY
from aerie_bench.checks.sidebar_frames import (FOLDER_HOST, SidebarFrames, seed_profile, state_json, summarise,
                                               tab_urls)

from tests.test_bench_frames import observation

FAKE = Path(__file__).parent / "bench_fake_browser.py"


def good():
    return observation()["animations"]


def bad_control():
    """What a folder of 300 tabs does: frames take hundreds of milliseconds and are dropped."""
    heavy = {"frames": 5, "frame_time_ms": {"p50": 668.2, "p95": 673.4, "p99": 673.4, "max": 673.4},
             "missed_frames": 60, "dropped_reports": 39, "dropped_frames": 60, "dropped_per_run": [12] * 5,
             "layout_frames": 0, "compositor_only": True}
    return {"folder_expand": heavy, "folder_collapse": heavy}


class SeedTest(unittest.TestCase):
    def test_the_seed_is_a_schema_1_state_with_one_collapsed_auto_folder(self):
        state = state_json(now=1_800_000_000)
        self.assertEqual(state["version"], 1)
        self.assertEqual(state["tabs"], [])
        folder = state["folders"][0]
        self.assertEqual((folder["kind"], folder["expanded"], folder["section"], folder["parent"]),
                         ("auto", False, "today", ""))
        self.assertEqual(folder["workspace_id"], state["workspace"]["id"])
        self.assertEqual(folder["rules"]["rules"][0]["value"], FOLDER_HOST)
        # Every record has the four fields ReadRecord needs, with times as strings of microseconds.
        for record in (state["workspace"], folder):
            for key in ("id", "created", "modified", "change"):
                self.assertIn(key, record)
        self.assertTrue(folder["created"].isdigit())

    def test_the_seed_is_written_where_the_tab_model_reads_it(self):
        with tempfile.TemporaryDirectory() as d:
            path = seed_profile(Path(d))
            self.assertEqual(path, Path(d) / "Default" / "AerieTabModel" / "state.json")
            self.assertEqual(json.loads(path.read_text())["version"], 1)

    def test_every_tab_is_on_a_localhost_name_the_folder_takes(self):
        urls = tab_urls("http://127.0.0.1:4321", 20)
        self.assertEqual(len(urls), 20)
        self.assertEqual(len(set(urls)), 20)
        for url in urls:
            host = url.split("/")[2].rsplit(":", 1)[0]
            self.assertTrue(host.endswith("." + FOLDER_HOST), url)
            self.assertTrue(host.endswith(".localhost"), url)

    def test_the_tab_fixture_exists_and_references_nothing_external(self):
        page = Path(__file__).parent.parent / "tools" / "aerie_bench" / "fixtures" / "sidebar_frames" / "tab.html"
        self.assertNotIn("http", page.read_text())


class RunTest(unittest.TestCase):
    """The check with a scripted observer instead of a browser on an Xvfb."""

    def setUp(self):
        self.dir = Path(tempfile.mkdtemp())
        self.addCleanup(lambda: __import__("shutil").rmtree(self.dir, ignore_errors=True))
        self.session = SimpleNamespace(out_dir=self.dir, artifacts=[], binary="/x/aerie", binary_version="1.2")
        patches = [mock.patch.object(xinput, "have_tools", return_value=None),
                   mock.patch.object(xinput, "xvfb", mock.MagicMock())]
        for p in patches:
            p.start()
            self.addCleanup(p.stop)
        self.calls = []

    def run_check(self, normal=None, reduced_anims=None, control=None, **options):
        normal = good() if normal is None else normal
        reduced_anims = ({k: v for k, v in good().items() if k.startswith("reduced_")}
                         if reduced_anims is None else reduced_anims)
        control = bad_control() if control is None else control

        check = SidebarFrames(**options)

        def fake(session, display, *, reduced: bool, tabs: int, groups=("hide_show", "folder", "resize")):
            self.calls.append({"reduced": reduced, "tabs": tabs, "groups": groups})
            if tabs > check.tabs:
                return dict(control), {}
            return (dict(reduced_anims) if reduced else {k: v for k, v in normal.items() if not k.startswith("reduced_")}), {}

        check.observe = fake
        return check.run(self.session)

    def test_a_clean_run_with_a_failing_control_passes(self):
        res = self.run_check()
        self.assertEqual(res.status, "pass", res.detail)
        self.assertIn("negative control failed as it must", res.detail)

    def test_the_run_is_three_browsers_normal_reduced_and_the_heavy_control(self):
        self.run_check(tabs=15, control_tabs=300)
        self.assertEqual([(c["reduced"], c["tabs"]) for c in self.calls], [(False, 15), (True, 15), (False, 300)])
        self.assertEqual(self.calls[2]["groups"], ("folder",))

    def test_a_dropped_frame_fails_the_check_and_names_the_animation(self):
        anims = good()
        anims["hide"] = anims["hide"] | {"dropped_frames": 10, "missed_frames": 10, "dropped_per_run": [10]}
        res = self.run_check(normal=anims)
        self.assertEqual(res.status, "fail")
        self.assertIn("hide: 10 dropped frames", res.detail)
        self.assertIn("Negative control", res.detail)  # a failing run still says that the control fired

    def test_a_control_that_passes_makes_the_check_fail_as_vacuous(self):
        res = self.run_check(control={"folder_expand": good()["folder_expand"], "folder_collapse": good()["folder_collapse"]})
        self.assertEqual(res.status, "fail")
        self.assertIn("negative control did not fail", res.detail)

    def test_a_control_that_only_fails_one_of_its_animations_still_counts(self):
        mixed = {"folder_expand": good()["folder_expand"], "folder_collapse": bad_control()["folder_collapse"]}
        self.assertEqual(self.run_check(control=mixed).status, "pass")

    def test_a_control_that_measured_nothing_is_a_harness_failure_not_a_pass_of_the_control(self):
        for broken in ({}, {"folder_expand": bad_control()["folder_expand"]},
                       {"folder_expand": bad_control()["folder_expand"],
                        "folder_collapse": bad_control()["folder_collapse"] | {"frames": 0}}):
            res = self.run_check(control=broken)
            self.assertEqual(res.status, "fail", broken)
            self.assertIn("negative control did not run", res.detail)
            self.assertNotIn("failed as it must", res.detail)

    def test_the_documents_are_written_and_listed(self):
        self.run_check()
        doc = json.loads((self.dir / "sidebar_frames.json").read_text())
        self.assertEqual(doc["refresh_hz"], 60)
        self.assertIn("show.p95_ms", doc["metrics"])
        self.assertIn("control", doc)
        self.assertEqual(self.session.artifacts, ["sidebar_frames.json", "sidebar_frames_traces.json"])

    def test_missing_tools_fail_with_what_is_missing(self):
        with mock.patch.object(xinput, "have_tools", return_value="Xvfb"):
            res = SidebarFrames().run(self.session)
        self.assertEqual(res.status, "fail")
        self.assertIn("Xvfb", res.detail)

    def test_the_summary_names_every_animation_with_its_numbers(self):
        text = summarise(good())
        for name in frames.animation_names():
            self.assertIn(name + ":", text)
        self.assertIn("p95", text)

    def baseline_file(self, **change):
        anims = good()
        for name, over in change.items():
            anims[name] = anims[name] | over
        path = self.dir / "main.json"
        path.write_text(json.dumps(frames.document(anims)))
        return str(path)

    def test_the_same_numbers_pass_against_the_baseline(self):
        res = self.run_check(baseline=self.baseline_file())
        self.assertEqual(res.status, "pass", res.detail)
        self.assertIn("within thresholds", res.detail)

    def test_a_slower_animation_than_the_baseline_fails(self):
        res = self.run_check(baseline=self.baseline_file(show={"frame_time_ms": {"p95": 0.5}}))
        self.assertEqual(res.status, "fail")
        self.assertIn("show.p95_ms", res.detail)

    def test_a_missing_baseline_fails_unless_the_first_run_flag_is_given(self):
        missing = str(self.dir / "nope.json")
        res = self.run_check(baseline=missing)
        self.assertEqual(res.status, "fail")
        self.assertIn("no baseline at", res.detail)
        self.assertEqual(self.run_check(baseline=missing, first_run=True).status, "pass")

    def test_a_broken_baseline_is_an_error_even_on_a_first_run(self):
        path = self.dir / "main.json"
        path.write_text("[]")
        self.assertEqual(self.run_check(baseline=str(path), first_run=True).status, "error")


class CliTest(unittest.TestCase):
    def setUp(self):
        self.root = Path(tempfile.mkdtemp())
        self.addCleanup(lambda: __import__("shutil").rmtree(self.root, ignore_errors=True))

    def invoke(self, *argv, check="sidebar_frames"):
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = cli.main(["--binary", str(FAKE), "--build-root", str(self.root), "--check", check, *argv])
        return code, out.getvalue(), err.getvalue()

    def test_the_check_is_registered(self):
        self.assertIn("sidebar_frames", REGISTRY)

    def test_its_flags_need_the_check(self):
        code, _, err = self.invoke("--sidebar-frames-record", str(self.root / "s"), check="smoke")
        self.assertEqual(code, 2)
        self.assertIn("need --check sidebar_frames", err)

    def test_first_run_needs_a_baseline_path(self):
        code, _, err = self.invoke("--sidebar-frames-first-run")
        self.assertEqual(code, 2)
        self.assertIn("--sidebar-frames-first-run needs --sidebar-frames-baseline", err)

    def test_repeats_must_be_two_or_more(self):
        code, _, err = self.invoke("--sidebar-frames-repeats", "1")
        self.assertEqual(code, 2)

    def run_recording(self, status_ok: bool):
        store = self.root / "store"

        def fake_run(self_, session):
            doc = frames.document(good(), binary={"path": "x"}, started="t")
            (session.out_dir / "sidebar_frames.json").write_text(json.dumps(doc))
            from aerie_bench.session import CheckResult
            return CheckResult("sidebar_frames", "pass" if status_ok else "fail", "x")

        with mock.patch.object(SidebarFrames, "run", fake_run):
            code, out, _ = self.invoke("--sidebar-frames-record", str(store))
        return code, out, store

    def test_a_passing_run_is_recorded_as_the_baseline(self):
        code, out, store = self.run_recording(True)
        self.assertEqual(code, 0, out)
        self.assertTrue((store / frames.MAIN_FILE).is_file())
        self.assertIn("recorded in", out)

    def test_a_failing_run_is_not_recorded(self):
        code, out, store = self.run_recording(False)
        self.assertEqual(code, 1)
        self.assertIn("not recorded", out)
        self.assertFalse((store / frames.MAIN_FILE).exists())


if __name__ == "__main__":
    unittest.main()


class FakeUi:
    """UI DevTools with a sidebar that is open or not, and a folder with some rows."""

    def __init__(self, open_=True, rows=3):
        self.open, self.rows = open_, rows

    def screen_rect(self, cls, within=None):
        return (0, 0, 280 if self.open else 0, 700) if cls == "VerticalTabStripRegionView" else (0, 0, 10, 10)

    def nodes_within(self, ancestor):
        return iter([(i, "AerieSidebarRowView") for i in range(self.rows)] + [(99, "AerieSidebarTopRow")])


class FakeXin:
    """XInput that returns the time of its press, as the real one does, and changes the fake UI when told to."""

    def __init__(self, ui=None, works=True):
        self.ui, self.works, self.presses = ui, works, 0

    def chord(self, *keys):
        self.presses += 1
        if self.works:
            self.ui.open = not self.ui.open
        return 1_000_000 + self.presses * 10_000_000  # the press, long after the call started

    click = chord

    def drag(self, *a):
        return 7_000_000


def observer(ui, xin, repeats=2):
    from aerie_bench.checks import sidebar_frames as sf
    obs = sf.Observer(SimpleNamespace(), ui, xin, repeats=repeats)
    obs.collapsed_rows = 3
    obs.record = lambda label, action: {"trace": {}, "result": action()}
    return obs


class ObserverTest(unittest.TestCase):
    def setUp(self):
        from aerie_bench.checks import sidebar_frames as sf
        patch = mock.patch.object(sf.time, "sleep")
        patch.start()
        self.addCleanup(patch.stop)

    def test_a_window_starts_at_the_first_input_event_the_action_reports_not_before_the_action(self):
        ui = FakeUi()
        obs = observer(ui, FakeXin(ui))
        seen = []
        with mock.patch.object(frames, "measure_runs", side_effect=lambda trace, starts, dur: seen.append(list(starts)) or {}):
            obs.hide_show(reduced=False)
        # hide is the 1st, 3rd press; show the 2nd and 4th
        self.assertEqual(seen, [[11_000_000, 31_000_000], [21_000_000, 41_000_000]])

    def test_the_window_of_a_drag_starts_at_the_first_move(self):
        ui = FakeUi()
        obs = observer(ui, FakeXin(ui))
        obs.ui = SimpleNamespace(screen_rect=lambda cls, within=None: (5, 0, 5, 700))
        seen = []
        with mock.patch.object(frames, "measure_runs", side_effect=lambda trace, starts, dur: seen.append(list(starts)) or {}):
            obs.resize()
        self.assertTrue(seen and all(x == 7_000_000 for x in seen[0]))

    def test_a_key_that_did_nothing_fails_the_run_with_a_clear_error(self):
        from aerie_bench.checks.sidebar_frames import InputLost
        ui = FakeUi()
        obs = observer(ui, FakeXin(ui, works=False))
        with self.assertRaisesRegex(InputLost, "hide: the input was lost.*still visible"):
            obs.hide_show(reduced=False)

    def test_a_click_that_did_not_expand_the_folder_fails_the_run(self):
        from aerie_bench.checks.sidebar_frames import InputLost
        ui = FakeUi(rows=3)  # no new rows after the click
        obs = observer(ui, FakeXin(ui, works=False))
        obs.folder_point = (10, 10)
        with self.assertRaisesRegex(InputLost, "folder_expand: the input was lost.*still collapsed"):
            obs.folder(reduced=False)

    def test_the_expand_of_a_huge_folder_is_not_read_but_a_lost_expand_is_caught_by_the_collapse(self):
        from aerie_bench.checks.sidebar_frames import InputLost
        ui = FakeUi(rows=3)

        class Xin(FakeXin):
            def chord(self, *keys):  # an honest click: odd presses expand, even ones collapse
                self.presses += 1
                ui.rows = 18 if self.presses % 2 == 1 else 3
                return 1_000_000 * self.presses
            click = chord

        obs = observer(ui, Xin(ui), repeats=2)
        obs.read_expanded = False
        obs.folder_point = (1, 1)
        with mock.patch.object(frames, "measure_runs", return_value={}):
            obs.folder(reduced=False)  # honest toggling passes without reading the expanded tree
        ui2 = FakeUi(rows=3)
        obs2 = observer(ui2, FakeXin(ui2, works=False))
        obs2.read_expanded = False
        obs2.folder_point = (1, 1)
        obs2.xin.click = lambda *a: 5
        ui2.rows = 18  # the folder stays expanded: the collapse check fails
        with mock.patch.object(frames, "measure_runs", return_value={}):
            with self.assertRaisesRegex(InputLost, "folder_collapse: the input was lost"):
                obs2.folder(reduced=False)

    def test_an_expanded_folder_shows_more_rows_than_the_collapsed_one(self):
        ui = FakeUi(rows=3)
        obs = observer(ui, FakeXin(ui))
        self.assertFalse(obs.folder_expanded())
        ui.rows = 18
        self.assertTrue(obs.folder_expanded())

    def test_a_hidden_sidebar_has_no_width(self):
        ui = FakeUi(open_=False)
        self.assertFalse(observer(ui, FakeXin(ui)).sidebar_open())
