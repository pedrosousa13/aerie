import json
import os
import shutil
import tempfile
import threading
import unittest
from pathlib import Path
from unittest import mock

from aerie_build import layers, patches, substitution
from tests.test_substitution import GRD, XTB, _msg, fake_fp

OLD_NS = 1_000_000_000_000_000_000  # an mtime far in the past


def patch_text(target: str, old: str, new: str) -> str:
    return f"--- a/{target}\n+++ b/{target}\n@@ -1 +1 @@\n-{old}\n+{new}\n"


class Fixture(unittest.TestCase):
    """A small tree, an Aerie patch dir, and the two real layers over them."""

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        root = Path(self._tmp.name)
        self.tree = root / "tree"
        self.store = root / "pristine"
        self.stage = root / "stage"
        self.pdir = root / "patches"
        self.pdir.mkdir()
        (self.pdir / "series").write_text("")
        for name in ("a", "b", "c"):
            self.write(f"src/{name}.txt", f"{name}-old\n")
        self.write("chrome/s.grd", GRD)
        fp = fake_fp(_msg("About Helium"))
        self.write("chrome/resources/s_de.xtb", XTB.replace('id="111"', f'id="{fp}"'))
        self.write("out/Default/keep.txt", "build output\n")
        self.sub = True
        self.workers = 1

    def tearDown(self):
        self._tmp.cleanup()

    def write(self, rel, text):
        path = self.tree / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)
        os.utime(path, ns=(OLD_NS, OLD_NS))

    def add_patch(self, name, target, new, old=None, listed=True):
        (self.pdir / name).write_text(patch_text(target, old or f"{Path(target).stem}-old", new))
        if listed:
            with open(self.pdir / "series", "a") as fh:
                fh.write(name + "\n")

    def set_series(self, *names):
        (self.pdir / "series").write_text("".join(n + "\n" for n in names))

    def layer_list(self):
        found = [patches.PatchLayer(patches.Layer("aerie", self.pdir, fuzz=False))]
        if self.sub:
            found.append(substitution.SubstitutionLayer(lambda: fake_fp, workers=self.workers))
        return found

    def run_layers(self, dry_run=False):
        return layers.apply_layers(self.tree, self.store, self.layer_list(), self.stage, dry_run=dry_run)

    def mtimes(self):
        return {str(p.relative_to(self.tree)): p.stat().st_mtime_ns
                for p in self.tree.rglob("*") if p.is_file()}

    def text(self, rel):
        return (self.tree / rel).read_text()

    def crash_at(self, n):
        real = layers.write_atomic
        count = {"n": 0}

        def flaky(path, data, **kw):
            if count["n"] >= n:
                raise KeyboardInterrupt("simulated interrupt")
            count["n"] += 1
            real(path, data, **kw)

        return mock.patch.object(layers, "write_atomic", flaky)


class IncrementalTest(Fixture):
    def test_rerun_without_changes_writes_nothing(self):
        self.add_patch("a.patch", "src/a.txt", "A")
        self.run_layers()
        before = self.mtimes()
        manifest = (self.store / layers.MANIFEST).stat().st_mtime_ns
        report = self.run_layers()
        self.assertTrue(report.idle)
        self.assertEqual(report.snapshots, 0)
        self.assertEqual(self.mtimes(), before)
        self.assertEqual((self.store / layers.MANIFEST).stat().st_mtime_ns, manifest)

    def test_first_run_changes_only_what_layers_touch(self):
        self.add_patch("a.patch", "src/a.txt", "A")
        report = self.run_layers()
        self.assertEqual(sorted(c.path for c in report.changes),
                         ["chrome/resources/s_de.xtb", "chrome/s.grd", "src/a.txt"])
        for rel in ("src/b.txt", "src/c.txt", "out/Default/keep.txt"):
            self.assertEqual(self.tree.joinpath(rel).stat().st_mtime_ns, OLD_NS)
        self.assertEqual(self.text("src/a.txt"), "A\n")
        self.assertIn("About Aerie", self.text("chrome/s.grd"))

    def test_changing_one_patch_rewrites_only_its_files(self):
        self.add_patch("a.patch", "src/a.txt", "A")
        self.add_patch("b.patch", "src/b.txt", "B")
        self.run_layers()
        before = self.mtimes()
        self.add_patch("b.patch", "src/b.txt", "BB", listed=False)
        report = self.run_layers()
        self.assertEqual([(c.path, c.kind) for c in report.changes], [("src/b.txt", "modify")])
        after = self.mtimes()
        self.assertNotEqual(after["src/b.txt"], before["src/b.txt"])
        self.assertEqual({k: v for k, v in after.items() if k != "src/b.txt"},
                         {k: v for k, v in before.items() if k != "src/b.txt"})
        self.assertEqual(self.text("src/b.txt"), "BB\n")

    def test_edit_is_applied_to_pristine_not_to_the_previous_result(self):
        self.add_patch("a.patch", "src/a.txt", "A")
        self.run_layers()
        self.add_patch("a.patch", "src/a.txt", "A2", listed=False)  # still expects "a-old"
        self.run_layers()
        self.assertEqual(self.text("src/a.txt"), "A2\n")

    def test_removing_a_patch_restores_pristine(self):
        self.add_patch("a.patch", "src/a.txt", "A")
        self.add_patch("b.patch", "src/b.txt", "B")
        self.run_layers()
        self.set_series("a.patch")
        before = self.mtimes()
        report = self.run_layers()
        self.assertEqual([(c.path, c.kind) for c in report.changes], [("src/b.txt", "restore")])
        self.assertEqual(self.text("src/b.txt"), "b-old\n")
        self.assertEqual(self.text("src/a.txt"), "A\n")
        self.assertEqual(self.mtimes()["src/a.txt"], before["src/a.txt"])
        self.assertNotIn("src/b.txt", json.loads((self.store / layers.MANIFEST).read_text()))
        self.assertFalse((self.store / layers.FILES / "src" / "b.txt").exists())

    def test_removing_every_patch_leaves_the_tree_as_pristine(self):
        self.sub = False
        self.add_patch("a.patch", "src/a.txt", "A")
        self.run_layers()
        self.set_series()
        self.run_layers()
        self.assertEqual(self.text("src/a.txt"), "a-old\n")

    def test_a_file_a_patch_creates_is_deleted_when_the_patch_goes(self):
        (self.pdir / "n.patch").write_text("--- /dev/null\n+++ b/src/new.txt\n@@ -0,0 +1 @@\n+hello\n")
        self.set_series("n.patch")
        report = self.run_layers()
        self.assertIn(("src/new.txt", "create"), [(c.path, c.kind) for c in report.changes])
        self.assertEqual(self.text("src/new.txt"), "hello\n")
        self.set_series()
        report = self.run_layers()
        self.assertIn(("src/new.txt", "remove"), [(c.path, c.kind) for c in report.changes])
        self.assertFalse((self.tree / "src" / "new.txt").exists())

    def test_a_patch_that_deletes_a_file_and_its_removal(self):
        (self.pdir / "d.patch").write_text("--- a/src/c.txt\n+++ /dev/null\n@@ -1 +0,0 @@\n-c-old\n")
        self.set_series("d.patch")
        self.run_layers()
        self.assertFalse((self.tree / "src" / "c.txt").exists())
        self.set_series()
        self.run_layers()
        self.assertEqual(self.text("src/c.txt"), "c-old\n")

    def test_file_mode_survives_a_rewrite(self):
        self.sub = False
        self.add_patch("a.patch", "src/a.txt", "A")
        (self.tree / "src" / "a.txt").chmod(0o755)
        self.run_layers()
        self.assertEqual((self.tree / "src" / "a.txt").stat().st_mode & 0o777, 0o755)

    def test_a_patch_that_does_not_apply_changes_nothing(self):
        self.add_patch("a.patch", "src/a.txt", "A")
        self.add_patch("b.patch", "src/b.txt", "B", old="not there")
        before = self.mtimes()
        with self.assertRaises(patches.PatchStateError) as ctx:
            self.run_layers()
        self.assertIn("b.patch", str(ctx.exception))
        self.assertEqual(self.mtimes(), before)
        self.assertFalse(self.store.exists())

    def test_snapshot_holds_pristine_bytes_before_layers_touch_them(self):
        self.add_patch("a.patch", "src/a.txt", "A")
        self.run_layers()
        self.assertEqual((self.store / layers.FILES / "src" / "a.txt").read_text(), "a-old\n")
        self.assertEqual((self.store / layers.FILES / "chrome" / "s.grd").read_text(), GRD)
        self.assertFalse((self.store / layers.FILES / "src" / "b.txt").exists())

    def test_damaged_snapshot_is_reported(self):
        self.add_patch("a.patch", "src/a.txt", "A")
        self.run_layers()
        (self.store / layers.FILES / "src" / "a.txt").write_text("tampered\n")
        with self.assertRaises(layers.LayerError) as ctx:
            self.run_layers()
        self.assertIn("--clean", str(ctx.exception))


class DryRunTest(Fixture):
    def test_reports_changes_and_writes_nothing(self):
        self.add_patch("a.patch", "src/a.txt", "A")
        before = self.mtimes()
        report = self.run_layers(dry_run=True)
        self.assertEqual(sorted(c.path for c in report.changes),
                         ["chrome/resources/s_de.xtb", "chrome/s.grd", "src/a.txt"])
        self.assertEqual(self.mtimes(), before)
        self.assertFalse(self.store.exists())
        self.assertEqual(self.text("src/a.txt"), "a-old\n")

    def test_after_a_real_run_it_reports_nothing(self):
        self.add_patch("a.patch", "src/a.txt", "A")
        self.run_layers()
        self.assertTrue(self.run_layers(dry_run=True).idle)

    def test_reports_restores(self):
        self.add_patch("a.patch", "src/a.txt", "A")
        self.run_layers()
        self.set_series()
        report = self.run_layers(dry_run=True)
        self.assertIn(("src/a.txt", "restore"), [(c.path, c.kind) for c in report.changes])
        self.assertEqual(self.text("src/a.txt"), "A\n")


class InterruptTest(Fixture):
    def reference(self):
        """The tree an uninterrupted run produces."""
        shutil.copytree(self.tree, self.tree.parent / "ref-tree")
        ref = self.tree
        other_store = self.store
        self.tree = self.tree.parent / "ref-tree"
        self.store = self.store.parent / "ref-store"
        self.run_layers()
        done = self.snapshot()
        self.tree, self.store = ref, other_store
        return done

    def snapshot(self):
        return {str(p.relative_to(self.tree)): p.read_bytes() for p in self.tree.rglob("*") if p.is_file()}

    def test_an_interrupt_anywhere_recovers_on_the_next_run(self):
        self.add_patch("a.patch", "src/a.txt", "A")
        self.add_patch("b.patch", "src/b.txt", "B")
        expected = self.reference()
        original = self.snapshot()
        # journal + 3 snapshots... + manifest + 5 tree writes + manifest: try every cut point.
        crashed = 0
        for n in range(0, 14):
            shutil.rmtree(self.tree)
            shutil.rmtree(self.store, ignore_errors=True)
            for rel, data in original.items():
                path = self.tree / rel
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(data)
            try:
                with self.crash_at(n):
                    self.run_layers()
            except KeyboardInterrupt:
                crashed += 1
                if n >= 1:
                    self.assertTrue((self.store / layers.JOURNAL).exists(), n)
            logs = []
            report = layers.apply_layers(self.tree, self.store, self.layer_list(), self.stage, logs.append)
            self.assertEqual(self.snapshot(), expected, n)
            self.assertFalse((self.store / layers.JOURNAL).exists(), n)
            self.assertEqual(list(self.tree.rglob("*" + layers.TMP_SUFFIX)), [], n)
            self.assertTrue(self.run_layers().idle, n)
        self.assertGreater(crashed, 8)

    def test_recovery_is_announced_and_stale_temp_files_are_removed(self):
        self.add_patch("a.patch", "src/a.txt", "A")
        with self.crash_at(1), self.assertRaises(KeyboardInterrupt):
            self.run_layers()
        stale = self.tree / "src" / ("a.txt" + layers.TMP_SUFFIX)
        stale.write_text("half")
        logs = []
        report = layers.apply_layers(self.tree, self.store, self.layer_list(), self.stage, logs.append)
        self.assertTrue(report.recovered)
        self.assertTrue(any("interrupted" in line for line in logs))
        self.assertFalse(stale.exists())

    def test_interrupt_while_restoring_after_a_patch_is_removed(self):
        self.sub = False
        self.add_patch("a.patch", "src/a.txt", "A")
        self.add_patch("b.patch", "src/b.txt", "B")
        self.run_layers()
        self.set_series()
        with self.crash_at(1), self.assertRaises(KeyboardInterrupt):  # journal ok, first restore dies
            self.run_layers()
        self.run_layers()
        self.assertEqual((self.text("src/a.txt"), self.text("src/b.txt")), ("a-old\n", "b-old\n"))
        self.assertFalse((self.store / layers.JOURNAL).exists())


class StaleEntryCrashTest(Fixture):
    """A store that already has stale entries: the manifest is saved before snapshot files go."""

    def setup_stale(self):
        self.sub = False
        self.add_patch("a.patch", "src/a.txt", "A")
        self.add_patch("b.patch", "src/b.txt", "B")
        self.run_layers()
        self.set_series()  # both entries are stale now

    def test_crash_at_every_write_while_dropping_stale_entries_recovers(self):
        for n in range(0, 6):
            self.tearDown()
            self.setUp()
            self.setup_stale()
            try:
                with self.crash_at(n):
                    self.run_layers()
            except KeyboardInterrupt:
                pass
            self.run_layers()
            self.assertEqual((self.text("src/a.txt"), self.text("src/b.txt")), ("a-old\n", "b-old\n"), n)
            self.assertEqual(json.loads((self.store / layers.MANIFEST).read_text()), {}, n)
            self.assertFalse((self.store / layers.JOURNAL).exists(), n)

    def test_crash_after_the_manifest_is_saved_but_before_snapshot_files_go(self):
        self.setup_stale()
        with mock.patch.object(layers.Store, "drop_files", side_effect=KeyboardInterrupt), \
             self.assertRaises(KeyboardInterrupt):
            self.run_layers()
        self.assertEqual(json.loads((self.store / layers.MANIFEST).read_text()), {})
        self.run_layers()  # loads fine: orphan snapshot files are ignored
        self.assertEqual(self.text("src/a.txt"), "a-old\n")
        self.assertFalse((self.store / layers.JOURNAL).exists())

    def test_the_manifest_never_lists_a_missing_snapshot_file(self):
        self.setup_stale()
        real = layers.Store.drop_files

        def check_then_drop(store, rels):
            manifest = json.loads((store.dir / layers.MANIFEST).read_text())
            self.assertEqual(manifest, {})  # already saved without the stale entries
            real(store, rels)

        with mock.patch.object(layers.Store, "drop_files", check_then_drop):
            self.run_layers()


class LockTest(Fixture):
    def test_concurrent_runs_are_serialised_and_leave_one_consistent_result(self):
        self.sub = False
        self.add_patch("a.patch", "src/a.txt", "A")
        inside = threading.Event()
        release = threading.Event()
        order = []

        class Blocker:
            name = "blocker"

            def __init__(self, tag, wait):
                self.tag, self.wait = tag, wait

            def apply(self, ws):
                order.append(f"{self.tag} in")
                if self.wait:
                    inside.set()
                    release.wait(5)
                order.append(f"{self.tag} out")

        def run(tag, wait):
            layers.apply_layers(self.tree, self.store, [*self.layer_list(), Blocker(tag, wait)], self.stage)

        first = threading.Thread(target=run, args=("first", True))
        second = threading.Thread(target=run, args=("second", False))
        first.start()
        self.assertTrue(inside.wait(5))
        second.start()
        second.join(0.5)
        self.assertTrue(second.is_alive())  # waiting on the lock
        self.assertEqual(order, ["first in"])
        release.set()
        first.join(5)
        second.join(5)
        self.assertEqual(order, ["first in", "first out", "second in", "second out"])
        self.assertEqual(self.text("src/a.txt"), "A\n")
        self.assertTrue(self.run_layers().idle)

    def test_each_run_stages_in_its_own_directory(self):
        seen = []

        class Spy:
            name = "spy"

            def apply(self, ws):
                seen.append(ws.stage_dir)

        for _ in range(2):
            layers.apply_layers(self.tree, self.store, [Spy()], self.stage)
        self.assertNotEqual(seen[0], seen[1])
        self.assertEqual(list(self.stage.iterdir()), [])  # cleaned up


class ClearStoreTest(Fixture):
    def test_clearing_waits_for_a_running_apply(self):
        self.add_patch("a.patch", "src/a.txt", "A")
        self.run_layers()
        inside, release = threading.Event(), threading.Event()

        class Hold:
            name = "hold"

            def apply(self, ws):
                inside.set()
                release.wait(5)

        runner = threading.Thread(
            target=lambda: layers.apply_layers(self.tree, self.store, [Hold()], self.stage, dry_run=True))
        runner.start()
        self.assertTrue(inside.wait(5))
        clearer = threading.Thread(target=layers.clear_store, args=(self.store,))
        clearer.start()
        clearer.join(0.5)
        self.assertTrue(clearer.is_alive())
        self.assertTrue(self.store.exists())
        release.set()
        runner.join(5)
        clearer.join(5)
        self.assertFalse(self.store.exists())


class PatchLayerRefusalTest(Fixture):
    def test_rename_headers_are_refused_and_the_tree_is_untouched(self):
        (self.pdir / "r.patch").write_text(
            "diff --git a/src/a.txt b/src/z.txt\nrename from src/a.txt\nrename to src/z.txt\n")
        self.set_series("r.patch")
        before = self.mtimes()
        with self.assertRaises(patches.PatchStateError) as err:
            self.run_layers()
        self.assertIn("rename", str(err.exception))
        self.assertEqual(self.mtimes(), before)

    def test_every_failing_patch_is_named_not_only_the_first(self):
        self.add_patch("a.patch", "src/a.txt", "A", old="nope")
        self.add_patch("b.patch", "src/b.txt", "B", old="nope")
        with self.assertRaises(patches.PatchStateError) as err:
            self.run_layers()
        self.assertIn("a.patch", str(err.exception))
        self.assertIn("b.patch", str(err.exception))


class ModeTest(Fixture):
    def test_restoring_a_deleted_file_brings_back_its_mode(self):
        (self.pdir / "d.patch").write_text("--- a/src/c.txt\n+++ /dev/null\n@@ -1 +0,0 @@\n-c-old\n")
        (self.tree / "src" / "c.txt").chmod(0o755)
        self.sub = False
        self.set_series("d.patch")
        self.run_layers()
        self.set_series()
        self.run_layers()
        self.assertEqual((self.tree / "src" / "c.txt").stat().st_mode & 0o777, 0o755)


class SubstitutionLayerTest(Fixture):
    def test_idempotent_with_zero_writes_on_rerun(self):
        first = self.run_layers()
        self.assertEqual(sorted(c.path for c in first.changes), ["chrome/resources/s_de.xtb", "chrome/s.grd"])
        self.assertIn("Über Aerie", self.text("chrome/resources/s_de.xtb"))
        before = self.mtimes()
        again = self.run_layers()
        self.assertTrue(again.idle)
        self.assertEqual(self.mtimes(), before)

    def test_matches_a_single_pass_over_the_pristine_text(self):
        self.run_layers()
        expected, _ = substitution.substitute_grd_text(GRD, fake_fp)
        self.assertEqual(self.text("chrome/s.grd"), expected)

    def test_substitution_sees_what_a_patch_changed_in_a_string_file(self):
        (self.pdir / "g.patch").write_text(
            "--- a/chrome/s.grd\n+++ b/chrome/s.grd\n@@ -5,3 +5,3 @@\n"
            "       <message name=\"IDS_PRODUCT_NAME\" desc=\"Helium is in this description\">\n"
            "-        Helium\n+        Helium Extra\n       </message>\n")
        self.set_series("g.patch")
        self.run_layers()
        self.assertIn("Aerie Extra", self.text("chrome/s.grd"))
        self.set_series()
        self.run_layers()
        self.assertEqual(self.text("chrome/s.grd"), substitution.substitute_grd_text(GRD, fake_fp)[0])

    def test_ignored_directories_are_left_alone(self):
        self.write("ash/x.grd", GRD)
        self.write("out/Default/y.grd", GRD)
        self.run_layers()
        self.assertEqual(self.text("ash/x.grd"), GRD)
        self.assertEqual(self.text("out/Default/y.grd"), GRD)


class WorkspaceSafetyTest(Fixture):
    def test_paths_outside_the_tree_are_refused(self):
        ws = layers.Workspace(layers.Store(self.store).reader(self.tree), self.stage)
        for bad in ("../x", "/etc/passwd", ""):
            with self.assertRaises(layers.LayerError):
                ws.put(bad, b"x")


if __name__ == "__main__":
    unittest.main()
