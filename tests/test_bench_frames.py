"""Sidebar animation frame times (issue #195): trace reading, measuring, judging, baseline, documented limits."""

import copy
import json
import tempfile
import unittest
from pathlib import Path

from aerie_bench import frames

ROOT = Path(__file__).parent.parent
DATA = ROOT / "tools" / "aerie_bench" / "testdata" / "sidebar_frames"
VSYNC_US = frames.VSYNC_MS * 1000


def fixture(name):
    return json.loads((DATA / name).read_text())


def steady(n=15, start=100_000, first_time_us=3000, frame_time_us=2000):
    """A trace of an animation with every frame on a 60 Hz beat."""
    draws = [int(start + i * VSYNC_US) for i in range(n)]
    pipeline = [[d - 2000, first_time_us if i == 0 else frame_time_us, "STATE_PRESENTED_ALL"]
                for i, d in enumerate(draws)]
    return {"pipeline": pipeline, "draws": draws, "layouts": [draws[0] - 500, draws[-1] + 500], "steps": []}


def observation(**over):
    """A passing observation of every animation, with ``over`` replacing single animations."""
    one = {"frames": 15, "frame_time_ms": {"p50": 2.0, "p95": 3.0, "p99": 3.0, "max": 3.0},
           "missed_frames": 0, "dropped_reports": 0, "dropped_frames": 0, "dropped_per_run": [0],
           "layout_frames": 0, "compositor_only": True}
    anims = {name: copy.deepcopy(one) for name in frames.animation_names()}
    anims.update(over)
    return {"animations": anims}


class CondenseTest(unittest.TestCase):
    def reporter(self, ph, ts, pid, local, seq=None, state=None):
        ev = {"name": "PipelineReporter", "ph": ph, "ts": ts, "pid": pid, "id2": {"local": local}, "args": {}}
        if ph == "b":
            ev["args"] = {"frame_reporter": {"frame_sequence": seq, "state": state}}
        return ev

    def test_begin_and_end_pair_into_one_frame_with_its_state_and_duration(self):
        events = [self.reporter("b", 1000, 1, "0x1", 7, "STATE_PRESENTED_ALL"), self.reporter("e", 3500, 1, "0x1")]
        self.assertEqual(frames.condense(events)["pipeline"], [[1000, 2500, "STATE_PRESENTED_ALL"]])

    def test_the_two_processes_report_one_frame_and_the_worse_state_wins(self):
        events = [self.reporter("b", 1000, 1, "0x1", 7, "STATE_PRESENTED_ALL"), self.reporter("e", 3000, 1, "0x1"),
                  self.reporter("b", 1000, 2, "0x1", 7, "STATE_DROPPED"), self.reporter("e", 5000, 2, "0x1")]
        self.assertEqual(frames.condense(events)["pipeline"], [[1000, 4000, "STATE_DROPPED"]])

    def test_draws_layouts_and_animation_steps_are_collected(self):
        step = {"name": "Graphics.Pipeline", "ph": "X", "ts": 10,
                "args": {"chrome_graphics_pipeline": {"step": "STEP_DRAW_AND_SWAP"}}}
        other = {"name": "Graphics.Pipeline", "ph": "X", "ts": 11,
                 "args": {"chrome_graphics_pipeline": {"step": "STEP_ISSUE_BEGIN_FRAME"}}}
        events = [step, other, {"name": "View::LayoutImmediately", "ph": "X", "ts": 20},
                  {"name": "LayerAnimator::Step", "ph": "X", "ts": 30}, {"name": "Noise", "ph": "X", "ts": 40}]
        out = frames.condense(events)
        self.assertEqual((out["draws"], out["layouts"], out["steps"]), ([10], [20], [30]))


class PercentileTest(unittest.TestCase):
    def test_nearest_rank(self):
        values = list(range(1, 21))
        self.assertEqual(frames.percentile(values, 50), 10)
        self.assertEqual(frames.percentile(values, 95), 19)
        self.assertEqual(frames.percentile(values, 99), 20)
        self.assertIsNone(frames.percentile([], 95))

    def test_a_short_run_has_its_worst_frame_as_p95(self):
        self.assertEqual(frames.percentile([1, 1, 1, 19], 95), 19)


class MeasureTest(unittest.TestCase):
    def test_a_steady_animation_has_no_missed_frames_and_no_layout_in_the_middle(self):
        m = frames.measure(steady(), 90_000, 240)
        self.assertEqual((m["missed_frames"], m["dropped_frames"], m["layout_frames"]), (0, 0, 0))
        self.assertTrue(m["compositor_only"])
        self.assertEqual(m["frames"], 15)

    def test_a_late_frame_counts_the_vsyncs_it_skipped(self):
        trace = steady()
        trace["draws"] = trace["draws"][:5] + [d + 3 * int(VSYNC_US) for d in trace["draws"][5:]]
        m = frames.measure(trace, 90_000, 280)
        self.assertEqual(m["missed_frames"], 3)
        self.assertEqual(m["dropped_frames"], 3)

    def test_a_millisecond_of_jitter_is_not_a_drop(self):
        trace = steady()
        trace["draws"] = [d + (1500 if i % 2 else 0) for i, d in enumerate(trace["draws"])]
        self.assertEqual(frames.measure(trace, 90_000, 240)["missed_frames"], 0)

    def test_a_stall_before_the_nominal_end_counts(self):
        trace = steady(n=5)  # the animation should run 220 ms and stops after 5 frames
        m = frames.measure(trace, 90_000, 240)
        self.assertGreaterEqual(m["missed_frames"], 5)

    def test_nothing_on_screen_is_all_frames_missed(self):
        trace = {"pipeline": [], "draws": [], "layouts": [], "steps": []}
        m = frames.measure(trace, 0, 200)
        self.assertEqual(m["frames"], 0)
        self.assertEqual(m["missed_frames"], 12)

    def test_the_compositors_own_dropped_report_counts_when_no_gap_shows_it(self):
        trace = steady()
        trace["pipeline"][6][2] = "STATE_DROPPED"
        m = frames.measure(trace, 90_000, 240)
        self.assertEqual((m["missed_frames"], m["dropped_reports"], m["dropped_frames"]), (0, 1, 1))
        self.assertEqual(m["frames"], 14)

    def test_a_layout_in_every_frame_is_not_compositor_only(self):
        trace = steady()
        trace["layouts"] += [d + 300 for d in trace["draws"]]
        m = frames.measure(trace, 90_000, 240)
        self.assertFalse(m["compositor_only"])
        self.assertGreater(m["layout_frames"], 8)

    def test_a_layout_at_the_start_and_at_the_end_is_not_a_layout_per_frame(self):
        trace = steady()
        trace["layouts"] = [trace["draws"][0] - 3000, trace["draws"][0] + 100, trace["draws"][-1] + 400]
        self.assertTrue(frames.measure(trace, 90_000, 240)["compositor_only"])

    def test_frames_outside_the_window_are_not_counted(self):
        trace = steady(n=15, start=100_000)
        trace["draws"] += [5_000_000]
        trace["pipeline"].append([4_998_000, 40_000, "STATE_DROPPED"])
        m = frames.measure(trace, 90_000, 240)
        self.assertEqual(m["dropped_frames"], 0)

    def test_percentiles_are_over_the_presented_frames_pipeline_time(self):
        trace = steady(first_time_us=19_500)
        m = frames.measure(trace, 90_000, 240)
        self.assertEqual(m["frame_time_ms"]["max"], 19.5)
        self.assertEqual(m["frame_time_ms"]["p50"], 2.0)

    def test_pooling_sums_the_counts_and_takes_percentiles_over_all_frames(self):
        trace = steady(n=15, start=100_000)
        second = steady(n=15, start=1_000_000)
        merged = {k: trace[k] + second[k] for k in trace}
        merged["draws"] = [d for d in merged["draws"]]
        merged["draws"][20] += 3 * int(VSYNC_US)
        pooled = frames.measure_runs(merged, [90_000, 990_000], 240)
        self.assertEqual(pooled["runs"], 2)
        self.assertEqual(pooled["frames"], 30)
        self.assertGreaterEqual(pooled["dropped_frames"], 1)
        self.assertEqual(len(pooled["dropped_per_run"]), 2)


class RecordedTraceTest(unittest.TestCase):
    """Real traces from the Xvfb train box (testdata/sidebar_frames)."""

    def run_of(self, name):
        data = fixture(name)
        return frames.measure(data["trace"], data["start_us"], data["duration_ms"])

    def test_a_recorded_show_slide_has_every_frame_and_only_the_layer_animates(self):
        m = self.run_of("show-slide.json")
        self.assertEqual((m["missed_frames"], m["dropped_frames"]), (0, 0))
        self.assertTrue(m["compositor_only"])
        self.assertGreaterEqual(m["frames"], 15)
        self.assertLess(m["interval_ms"]["p95"], frames.VSYNC_MS * frames.MISSED_GAP_VSYNCS)

    def test_a_recorded_folder_expand_lays_out_per_frame_which_the_design_allows(self):
        m = self.run_of("folder-expand.json")
        self.assertFalse(m["compositor_only"])
        self.assertGreaterEqual(m["layout_frames"], 5)
        self.assertEqual(m["dropped_frames"], 0)

    def test_a_recorded_hide_that_put_no_frame_on_screen_is_all_missed(self):
        m = self.run_of("hide-no-frames.json")
        self.assertGreaterEqual(m["dropped_frames"], 8)


    def test_a_recorded_hide_in_steady_state_puts_one_frame_on_screen_in_200_ms(self):
        m = self.run_of("hide-one-frame.json")
        self.assertEqual(m["draws"], 1)
        self.assertGreaterEqual(m["dropped_frames"], 9)

    def test_a_recorded_folder_collapse_is_clean(self):
        m = self.run_of("folder-collapse.json")
        self.assertEqual(m["dropped_frames"], 0)

    def test_a_recorded_expand_of_a_folder_of_300_tabs_is_the_negative_control_and_fails(self):
        data = fixture("folder-expand-300-tabs.json")
        m = frames.measure_runs(data["trace"], [data["start_us"]], data["duration_ms"])
        problems = frames.judge({"animations": {"folder_expand": m}}, names=("folder_expand",))
        self.assertTrue(any("p95 frame time" in p for p in problems), problems)
        self.assertTrue(any("dropped frames" in p for p in problems), problems)


class JudgeTest(unittest.TestCase):
    def test_a_clean_observation_passes(self):
        self.assertEqual(frames.judge(observation()), [])

    def test_an_empty_observation_fails(self):
        self.assertTrue(frames.judge({}))

    def test_an_animation_that_was_not_measured_fails(self):
        obs = observation()
        del obs["animations"]["hide"]
        self.assertTrue(any("hide" in p and "not measured" in p for p in frames.judge(obs)))

    def test_a_dropped_frame_fails_and_names_the_animation(self):
        bad = observation()["animations"]["folder_expand"] | {"dropped_frames": 2, "missed_frames": 2,
                                                             "dropped_per_run": [0, 2]}
        problems = frames.judge(observation(folder_expand=bad))
        self.assertEqual(len(problems), 1)
        self.assertIn("folder_expand", problems[0])
        self.assertIn("2 dropped", problems[0])

    def test_a_p95_over_one_vsync_fails(self):
        slow = observation()["animations"]["show"] | {"frame_time_ms": {"p95": 17.5}}
        self.assertTrue(any("p95" in p for p in frames.judge(observation(show=slow))))

    def test_a_p95_of_exactly_the_limit_passes(self):
        edge = observation()["animations"]["show"] | {"frame_time_ms": {"p95": frames.P95_LIMIT_MS}}
        self.assertEqual(frames.judge(observation(show=edge)), [])

    def test_layout_in_a_slide_fails_but_in_a_folder_it_does_not(self):
        laid = {"compositor_only": False, "layout_frames": 9}
        base = observation()["animations"]
        self.assertTrue(any("layout" in p for p in frames.judge(observation(hide=base["hide"] | laid))))
        self.assertEqual(frames.judge(observation(folder_expand=base["folder_expand"] | laid)), [])

    def test_reduced_motion_is_judged_like_the_rest(self):
        bad = observation()["animations"]["reduced_hide"] | {"dropped_frames": 1}
        self.assertTrue(any(p.startswith("reduced_hide") for p in frames.judge(observation(reduced_hide=bad))))

    def test_an_animation_with_no_frame_fails(self):
        none = observation()["animations"]["show"] | {"frames": 0}
        self.assertTrue(any("no frame" in p for p in frames.judge(observation(show=none))))

    def test_the_recorded_hide_with_no_frames_fails_through_the_whole_chain(self):
        data = fixture("hide-no-frames.json")
        m = frames.measure_runs(data["trace"], [data["start_us"]], data["duration_ms"])
        self.assertTrue(frames.judge(observation(hide=m)))


class BaselineTest(unittest.TestCase):
    def doc(self, **over):
        anims = observation()["animations"]
        for name, change in over.items():
            anims[name] = anims[name] | change
        return frames.document(anims, binary={"path": "/x"}, started="2026-11-15T10:00:00+00:00")

    def status(self, cur, base):
        return {v["metric"]: v["status"] for v in frames.compare(cur, base)}

    def test_an_unchanged_run_is_ok(self):
        self.assertEqual(set(self.status(self.doc(), self.doc()).values()), {"ok"})

    def test_one_more_dropped_frame_is_a_regression(self):
        cur = self.doc(show={"dropped_frames": 1})
        self.assertEqual(self.status(cur, self.doc())["show.dropped_frames"], "regressed")
        self.assertEqual(len(frames.failures(frames.compare(cur, self.doc()))), 1)

    def test_fewer_dropped_frames_is_an_improvement(self):
        self.assertEqual(self.status(self.doc(), self.doc(show={"dropped_frames": 2}))["show.dropped_frames"],
                         "improved")

    def test_a_slower_p95_past_both_limits_regresses(self):
        cur = self.doc(hide={"frame_time_ms": {"p95": 5.5}})
        self.assertEqual(self.status(cur, self.doc())["hide.p95_ms"], "regressed")

    def test_a_slower_p95_inside_the_absolute_floor_is_noise(self):
        cur = self.doc(hide={"frame_time_ms": {"p95": 4.5}})  # +50% but 1.5 ms
        self.assertEqual(self.status(cur, self.doc())["hide.p95_ms"], "ok")

    def test_an_animation_the_run_lost_is_missing(self):
        cur = self.doc()
        del cur["metrics"]["show.p95_ms"]
        self.assertEqual(self.status(cur, self.doc())["show.p95_ms"], "missing")

    def test_a_new_animation_is_new(self):
        base = self.doc()
        del base["metrics"]["show.p95_ms"]
        self.assertEqual(self.status(self.doc(), base)["show.p95_ms"], "new")

    def test_the_failure_lines_name_the_limit(self):
        line = frames.verdict_lines(frames.failures(frames.compare(self.doc(hide={"frame_time_ms": {"p95": 9.0}}),
                                                                   self.doc())))[0]
        self.assertIn("hide.p95_ms", line)
        self.assertIn("limit 25%", line)


class DocumentsTest(unittest.TestCase):
    def test_record_appends_and_replaces_the_baseline(self):
        with tempfile.TemporaryDirectory() as d:
            doc = frames.document(observation()["animations"], started="x")
            main = frames.record(Path(d), doc)
            frames.record(Path(d), doc)
            self.assertEqual(frames.load(main)["schema"], frames.SCHEMA)
            self.assertEqual(len((Path(d) / frames.HISTORY_FILE).read_text().splitlines()), 2)

    def test_load_refuses_a_foreign_document(self):
        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / "x.json"
            path.write_text(json.dumps({"schema": 1, "metrics": {"startup_cold_ms": 1}, "foo": 1}))
            self.assertEqual(frames.load(path)["schema"], 1)  # the schema is shared with perf, the names are not
            path.write_text("[]")
            with self.assertRaises(ValueError):
                frames.load(path)


class DocsAndTokensTest(unittest.TestCase):
    def test_the_documented_tables_are_the_ones_in_the_code(self):
        text = (ROOT / "docs" / "bench.md").read_text()
        self.assertIn(frames.threshold_table(), text)
        self.assertIn(frames.absolute_table(), text)
        for name in frames.animation_names():
            self.assertIn(f"`{name}`", text)

    def test_the_nominal_durations_are_the_design_tokens(self):
        tokens = json.loads((ROOT / "prototypes" / "sidebar" / "tokens.json").read_text())["motion-duration"]
        want = {"show": "slow", "hide": "base", "folder_expand": "folder-reveal", "folder_collapse": "folder-collapse"}
        for anim, token in want.items():
            self.assertEqual(frames.ANIMATIONS[anim]["duration_ms"], tokens[token]["value"], anim)
            self.assertEqual(frames.ANIMATIONS[anim]["reduced_ms"], tokens[token]["reduced"], anim)

    def test_one_vsync_at_60_hz(self):
        self.assertAlmostEqual(frames.VSYNC_MS, 16.667, places=2)
        self.assertEqual(frames.P95_LIMIT_MS, 16.7)


if __name__ == "__main__":
    unittest.main()
