import hashlib
import json
import os
import subprocess
import tempfile
import threading
import time
import unittest
import zipfile
from pathlib import Path
from unittest import mock

from aerie_build import helium_inputs, lock, patch_check, patches
from aerie_build.config import BuildPaths


def make_tarball(root: Path, name: str, files: dict[str, str]) -> Path:
    src = root / f"src-{name}"
    for rel, text in files.items():
        path = src / "chromium-1" / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)
    tarball = root / f"{name}.tar"
    subprocess.run(["tar", "-cf", str(tarball), "-C", str(src), "chromium-1"], check=True)
    return tarball


class EnsureSparseTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.root = Path(self._tmp.name)
        self.base = make_tarball(self.root, "base", {"a/one.txt": "1", "a/two.txt": "2"})
        self.linux = make_tarball(self.root, "linux", {"b/three.txt": "3", "a/two.txt": "2-linux"})
        self.sparse = self.root / "sparse"
        self.verified: list[Path] = []
        self.sources = [
            patch_check.Source(self.base, "chromium-1", ""),
            patch_check.Source(self.linux, "chromium-1", ""),
        ]

    def tearDown(self):
        self._tmp.cleanup()

    def ensure(self, wanted, version="1", sources=None):
        return patch_check.ensure_sparse(
            self.sparse, version, wanted, sources or self.sources, verify=self.verified.append,
        )

    def test_extracts_from_every_source_and_returns_files_no_source_has(self):
        missing = self.ensure(["a/one.txt", "b/three.txt", "new/created.txt"])
        self.assertEqual(missing, ["new/created.txt"])
        self.assertEqual((self.sparse / "a/one.txt").read_text(), "1")
        self.assertEqual((self.sparse / "b/three.txt").read_text(), "3")
        self.assertFalse((self.sparse / "a/two.txt").exists())

    def test_later_source_overwrites_earlier_one_like_helium_unpack(self):
        self.ensure(["a/two.txt"])
        self.assertEqual((self.sparse / "a/two.txt").read_text(), "2-linux")

    def test_sources_are_verified_before_they_are_read(self):
        self.ensure(["a/one.txt"])
        self.assertEqual(self.verified, [self.base, self.linux])

    def test_second_run_with_same_files_reads_nothing(self):
        self.ensure(["a/one.txt", "new/created.txt"])
        self.verified.clear()
        self.ensure(["a/one.txt", "new/created.txt"])
        self.assertEqual(self.verified, [])

    def test_new_wanted_file_triggers_an_incremental_extract(self):
        self.ensure(["a/one.txt"])
        self.verified.clear()
        self.ensure(["a/one.txt", "b/three.txt"])
        self.assertEqual(len(self.verified), 2)
        self.assertTrue((self.sparse / "b/three.txt").exists())
        self.assertTrue((self.sparse / "a/one.txt").exists())

    def test_version_change_wipes_the_cache(self):
        self.ensure(["a/one.txt"], version="1")
        self.ensure(["b/three.txt"], version="2")
        self.assertFalse((self.sparse / "a/one.txt").exists())
        manifest = json.loads((self.sparse / patch_check.MANIFEST).read_text())
        self.assertEqual(manifest["chromium_version"], "2")

    def test_failed_first_population_is_not_trusted_after_the_pin_changes(self):
        def verify(archive):
            if archive == self.linux:
                raise RuntimeError("boom")
        with self.assertRaises(RuntimeError):
            patch_check.ensure_sparse(self.sparse, "1", ["a/one.txt", "b/three.txt"], self.sources, verify)
        self.assertTrue((self.sparse / "a/one.txt").exists())
        self.ensure(["b/three.txt"], version="1")
        self.assertFalse((self.sparse / "a/one.txt").exists())

    def test_failed_incremental_update_wipes_the_cache_next_time(self):
        self.ensure(["a/one.txt"])
        def verify(archive):
            raise RuntimeError("boom")
        with self.assertRaises(RuntimeError):
            patch_check.ensure_sparse(self.sparse, "1", ["a/one.txt", "b/three.txt"], self.sources, verify)
        self.ensure(["b/three.txt"])
        self.assertFalse((self.sparse / "a/one.txt").exists())

    def test_strip_or_output_change_wipes_the_cache(self):
        self.ensure(["a/one.txt"])
        moved = [patch_check.Source(self.base, "chromium-1", "x"), self.sources[1]]
        self.ensure(["b/three.txt"], sources=moved)
        self.assertFalse((self.sparse / "a/one.txt").exists())

    def test_unreadable_manifest_is_wiped_and_rebuilt(self):
        self.ensure(["a/one.txt"])
        (self.sparse / patch_check.MANIFEST).write_text("{not json")
        self.assertEqual(self.ensure(["a/one.txt"]), [])
        self.assertEqual((self.sparse / "a/one.txt").read_text(), "1")

    def test_zip_and_gzip_sources_land_under_their_output_path(self):
        zpath = self.root / "dep.zip"
        with zipfile.ZipFile(zpath, "w") as z:
            z.writestr("pkg/js/assets.js", "zip")
            z.writestr("pkg/other.js", "other")
        gz_src = self.root / "gz"
        (gz_src / "res").mkdir(parents=True)
        (gz_src / "res" / "x.gni").write_text("gz")
        gz = self.root / "dep.tar.gz"
        subprocess.run(["tar", "-czf", str(gz), "-C", str(gz_src), "."], check=True)  # members start with ./
        sources = [
            patch_check.Source(zpath, "pkg", "third_party/zipped"),
            patch_check.Source(gz, "", "components/gz"),
        ]
        missing = self.ensure(
            ["third_party/zipped/js/assets.js", "components/gz/res/x.gni", "components/gz/nope"], sources=sources
        )
        self.assertEqual(missing, ["components/gz/nope"])
        self.assertEqual((self.sparse / "third_party/zipped/js/assets.js").read_text(), "zip")
        self.assertEqual((self.sparse / "components/gz/res/x.gni").read_text(), "gz")
        self.assertFalse((self.sparse / "third_party/zipped/other.js").exists())


class SourcesFromIniTest(unittest.TestCase):
    def test_reads_filename_strip_and_output_path_from_helium_ini_files(self):
        with tempfile.TemporaryDirectory() as tmp:
            ini = Path(tmp) / "downloads.ini"
            ini.write_text(
                "[chromium]\ndownload_filename = c-%(_chromium_version)s.tar.zst\n"
                "output_path = ./\nstrip_leading_dirs = chromium-%(_chromium_version)s\n"
                "[dep]\ndownload_filename = d.zip\noutput_path = third_party/ublock\n"
                "strip_leading_dirs = uBlock0.chromium\n"
                "[plain]\ndownload_filename = p.tar.gz\noutput_path = ./components/p\n"
            )
            sources = patch_check.sources_from_ini([ini], "9.9", Path("/dl"))
            self.assertEqual(
                sources,
                [
                    patch_check.Source(Path("/dl/c-9.9.tar.zst"), "chromium-9.9", ""),
                    patch_check.Source(Path("/dl/d.zip"), "uBlock0.chromium", "third_party/ublock"),
                    patch_check.Source(Path("/dl/p.tar.gz"), "", "components/p"),
                ],
            )


class LayersTest(unittest.TestCase):
    def test_layer_order_and_fuzz(self):
        layers = patch_check.layers(Path("/r"))
        self.assertEqual([l.label for l in layers], ["helium-chromium", "helium-linux", "aerie"])
        self.assertEqual([l.fuzz for l in layers], [True, True, False])
        self.assertEqual(layers[2].directory, Path("/r/patches"))


PATCH = """Aerie-Purpose: test
Aerie-Upstream-Area: a
Aerie-Kind: tracer

--- a/a/one.txt
+++ b/a/one.txt
@@ -1 +1 @@
-1
+one
"""


class RunCheckTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        root = Path(self._tmp.name)
        self.repo = root / "repo"
        self.build = BuildPaths(root / "build")
        self.build.downloads.mkdir(parents=True)
        tarball = make_tarball(self.build.downloads, "base", {"a/one.txt": "1\n"})
        digest = hashlib.sha256(tarball.read_bytes()).hexdigest()
        for sub in ("helium-chromium", "helium-linux"):
            (self.repo / sub / "patches").mkdir(parents=True)
            (self.repo / sub / "patches" / "series").write_text("")
        (self.repo / "helium-chromium" / "downloads.ini").write_text(
            "[c]\ndownload_filename = base.tar\noutput_path = ./\nstrip_leading_dirs = chromium-1\n")
        (self.repo / "helium-linux" / "downloads.ini").write_text("")
        (self.repo / "helium-chromium" / "deps.ini").write_text("[ublock_origin]\n[onboarding]\n")
        (self.repo / "patches").mkdir()
        (self.repo / "patches" / "series").write_text("0001.patch\n")
        (self.repo / "patches" / "0001.patch").write_text(PATCH)
        self.pins = lock.Lock("1", "1", "t", "c", "t", "c", downloads={"base.tar": digest})
        steps = mock.patch.object(patch_check, "_helium_steps")  # no Helium utils in this fake repo
        self.steps = steps.start()
        self.addCleanup(steps.stop)

    def tearDown(self):
        self._tmp.cleanup()

    def run_check(self, log=None):
        logs: list[str] = []
        status = patch_check.run_check(self.repo, self.build, self.pins, log=log or logs.append)
        return status, logs

    def test_check_passes_and_leaves_no_work_dir(self):
        status, logs = self.run_check()
        self.assertEqual(status, 0, logs)
        self.assertIn("0 failed, 0 skipped, 1 applied", logs[-1])
        self.assertEqual([p.name for p in (self.build.root / "check").iterdir()
                          if p.name not in ("sparse", "sparse.lock")], [])

    def test_an_unrelated_lint_problem_still_uses_the_filtered_deps_ini(self):
        (self.repo / "patches" / "stray.patch").write_text(PATCH)  # not in series: a lint problem
        with mock.patch.object(patch_check.helium_deps, "write_filtered",
                               wraps=patch_check.helium_deps.write_filtered) as filtered:
            status, logs = self.run_check()
        self.assertEqual(status, 1, logs)
        self.assertTrue(any("not listed in series" in l for l in logs), logs)
        filtered.assert_called_once()

    def test_a_deps_ini_problem_falls_back_to_the_unfiltered_one(self):
        (self.repo / "helium-chromium" / "deps.ini").write_text("[other]\ndownload_filename = x.zip\noutput_path = ./x\n")  # no [ublock_origin]
        status, logs = self.run_check()
        self.assertEqual(status, 1, logs)
        self.assertTrue(any("ublock_origin" in l for l in logs), logs)

    def test_does_not_touch_a_fixed_work_dir(self):
        stale = self.build.root / "check" / "work"
        stale.mkdir(parents=True)
        (stale / "sentinel").write_text("x")
        status, _ = self.run_check()
        self.assertEqual(status, 0)
        self.assertEqual((stale / "sentinel").read_text(), "x")

    def test_work_dir_is_removed_when_the_check_fails(self):
        with mock.patch.object(patches, "apply_layers", side_effect=KeyboardInterrupt):
            with self.assertRaises(KeyboardInterrupt):
                self.run_check()
        leftovers = [p.name for p in (self.build.root / "check").iterdir()
                     if p.name not in ("sparse", "sparse.lock")]
        self.assertEqual(leftovers, [])

    def leftovers(self):
        return [p.name for p in (self.build.root / "check").iterdir()
                if p.name not in ("sparse", "sparse.lock")]

    def test_lock_is_released_and_work_dir_removed_when_copy_fails(self):
        with mock.patch.object(patches, "copy_tree", side_effect=RuntimeError("boom")):
            with self.assertRaises(RuntimeError):
                self.run_check()
        self.assertEqual(self.leftovers(), [])
        status, logs = self.run_check()  # would hang if the lock leaked
        self.assertEqual(status, 0, logs)

    def test_lock_is_released_when_ensure_sparse_fails(self):
        with mock.patch.object(patch_check, "ensure_sparse", side_effect=RuntimeError("boom")):
            with self.assertRaises(RuntimeError):
                self.run_check()
        self.assertEqual(self.leftovers(), [])
        self.assertEqual(self.run_check()[0], 0)

    def test_stale_work_dirs_are_swept_but_fresh_ones_kept(self):
        check = self.build.root / "check"
        old, fresh = check / "work-old", check / "work-fresh"
        for d in (old, fresh):
            d.mkdir(parents=True)
        t = time.time() - 2 * 3600
        os.utime(old, (t, t))
        self.assertEqual(self.run_check()[0], 0)
        self.assertFalse(old.exists())
        self.assertTrue(fresh.exists())

    def test_concurrent_checks_both_succeed(self):
        for _ in range(5):
            results: list = []
            barrier = threading.Barrier(4)

            def worker():
                barrier.wait()
                try:
                    results.append(self.run_check())
                except BaseException as exc:  # noqa: BLE001
                    results.append(exc)

            threads = [threading.Thread(target=worker) for _ in range(4)]
            for t in threads:
                t.start()
            for t in threads:
                t.join()
            self.assertEqual(len(results), 4)
            self.assertEqual(self.leftovers(), [])
            for result in results:
                self.assertNotIsInstance(result, BaseException, result)
                status, logs = result
                self.assertEqual(status, 0, logs)
                self.assertIn("0 failed, 0 skipped, 1 applied", logs[-1])


def _patch(old: str, new: str, path: str = "net/listed.cc") -> str:
    return (f"Aerie-Purpose: test\nAerie-Upstream-Area: net\n\n"
            f"--- a/{path}\n+++ b/{path}\n@@ -1 +1 @@\n-{old}\n+{new}\n")


class RealStepsTest(unittest.TestCase):
    """The check runs Helium's own substitution scripts between Helium's patches and Aerie's."""

    REAL_CORE = Path(__file__).resolve().parents[1] / "helium-chromium"

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        root = Path(self._tmp.name)
        self.repo = root / "repo"
        self.build = BuildPaths(root / "build")
        self.build.downloads.mkdir(parents=True)
        tarball = make_tarball(self.build.downloads, "base", {
            "net/listed.cc": "url www.google.com\n", "net/other.cc": "url www.google.com\n",
            "OWNERS": "x\n", "chrome/VERSION": "MAJOR=1\n", "tools/grit/grit/__init__.py": "",
        })
        digest = hashlib.sha256(tarball.read_bytes()).hexdigest()
        core, linux = self.repo / "helium-chromium", self.repo / "helium-linux"
        for sub in (core, linux):
            (sub / "patches").mkdir(parents=True)
            (sub / "patches" / "series").write_text("")
        (core / "utils").symlink_to(self.REAL_CORE / "utils")
        (core / "domain_regex.list").write_text(r"google([A-Za-z\-]*?\\*?)\.com#9oo91e\g<1>.qjz9zk" + "\n")
        (core / "domain_substitution.list").write_text("net/listed.cc\nnet/not-in-sparse.cc\n")
        (core / "version.txt").write_text("0.21\n")
        (core / "chromium_version.txt").write_text("157.0.1\n")
        (core / "revision.txt").write_text("1\n")
        (linux / "revision.txt").write_text("1\n")
        (core / "downloads.ini").write_text(
            "[c]\ndownload_filename = base.tar\noutput_path = ./\nstrip_leading_dirs = chromium-1\n")
        (linux / "downloads.ini").write_text("")
        (core / "deps.ini").write_text("[ublock_origin]\n[onboarding]\n")
        (self.repo / "patches").mkdir()
        self.pins = lock.Lock("1", "1", "t", "c", "t", "c", downloads={"base.tar": digest})

    def tearDown(self):
        self._tmp.cleanup()

    def aerie(self, *patch_texts):
        (self.repo / "patches" / "series").write_text("".join(f"{i}.patch\n" for i in range(len(patch_texts))))
        for i, text in enumerate(patch_texts):
            (self.repo / "patches" / f"{i}.patch").write_text(text)

    def check(self):
        logs: list[str] = []
        return patch_check.run_check(self.repo, self.build, self.pins, log=logs.append), logs

    def test_a_patch_written_against_the_substituted_text_applies(self):
        self.aerie(_patch("url www.9oo91e.qjz9zk", "url www.example.test"))
        status, logs = self.check()
        self.assertEqual(status, 0, logs)

    def test_a_patch_written_against_the_original_text_fails(self):
        self.aerie(_patch("url www.google.com", "url www.example.test"))
        status, logs = self.check()
        self.assertEqual(status, 1)
        self.assertIn("FAILED  aerie/0.patch", "\n".join(logs))

    def test_a_file_the_substitution_list_skips_keeps_its_original_text(self):
        self.aerie(_patch("url www.google.com", "url www.example.test", "net/other.cc"))
        self.assertEqual(self.check()[0], 0)

    def test_aerie_patches_see_the_version_lines_helium_appends(self):
        (self.repo / "patches").mkdir(exist_ok=True)
        text = ("Aerie-Purpose: t\nAerie-Upstream-Area: c\n\n--- a/chrome/VERSION\n+++ b/chrome/VERSION\n"
                "@@ -1,3 +1,3 @@\n MAJOR=1\n-HELIUM_MAJOR=0\n+HELIUM_MAJOR=9\n HELIUM_MINOR=21\n")
        self.aerie(text)
        status, logs = self.check()
        self.assertEqual(status, 0, logs)  # only possible when the step has already appended HELIUM_MAJOR

    def test_a_failing_step_is_an_error_not_a_pass(self):
        (self.repo / "helium-chromium" / "version.txt").unlink()
        self.aerie(_patch("url www.9oo91e.qjz9zk", "x"))
        with self.assertRaises(Exception):
            self.check()

    def test_helium_patch_failure_stops_before_the_aerie_patches(self):
        (self.repo / "helium-chromium" / "patches" / "series").write_text("h.patch\n")
        (self.repo / "helium-chromium" / "patches" / "h.patch").write_text(
            _patch("not there", "x", "net/other.cc"))
        self.aerie(_patch("url www.9oo91e.qjz9zk", "x"))
        status, logs = self.check()
        self.assertEqual(status, 1)
        self.assertTrue(any("not tried" in line for line in logs))


class PreparedTreeCheckTest(RealStepsTest):
    """With a prepared tree and a pristine store, the patches are also tried against it."""

    def setUp(self):
        super().setUp()
        self.tree = self.build.tree("helium-0.21.1.1")
        (self.tree / "net").mkdir(parents=True)
        (self.tree / "net" / "listed.cc").write_text("url www.9oo91e.qjz9zk\n")
        self.pins = lock.Lock("0.21.1.1", "1", "t", "c", "t", "c", downloads=self.pins.downloads)

    def stamp(self):
        (self.tree / ".resources.stamp").touch()
        (self.tree / helium_inputs.STAMP).write_text(helium_inputs.digest(self.repo) + "\n")

    def test_a_tree_built_from_other_helium_inputs_is_skipped_not_failed(self):
        (self.tree / ".resources.stamp").touch()
        (self.tree / helium_inputs.STAMP).write_text("0" * 64 + "\n")
        (self.tree / "net" / "listed.cc").write_text("url drifted\n")  # would fail if it were checked
        self.aerie(_patch("url www.9oo91e.qjz9zk", "x"))
        status, logs = self.check()
        self.assertEqual(status, 0, logs)
        self.assertIn(f"prepared tree {self.tree}: built from different Helium inputs; skipped", logs)

    def test_a_tree_without_the_inputs_stamp_is_skipped_too(self):
        (self.tree / ".resources.stamp").touch()
        self.aerie(_patch("url www.9oo91e.qjz9zk", "x"))
        status, logs = self.check()
        self.assertEqual(status, 0, logs)
        self.assertTrue(any("different Helium inputs; skipped" in line for line in logs))

    def test_a_damaged_snapshot_is_a_message_not_a_traceback(self):
        self.stamp()
        store = self.build.pristine / "helium-0.21.1.1"
        (store / "files" / "net").mkdir(parents=True)
        (store / "manifest.json").write_text(
            '{"net/listed.cc": {"present": true, "sha256": "bad", "mode": null}}')
        self.aerie(_patch("url www.9oo91e.qjz9zk", "x"))
        status, logs = self.check()
        self.assertEqual(status, 1)
        self.assertTrue(any("cannot be checked" in line and "--clean" in line for line in logs), logs)

    def test_no_stamp_means_no_prepared_tree_report(self):
        self.aerie(_patch("url www.9oo91e.qjz9zk", "x"))
        status, logs = self.check()
        self.assertEqual(status, 0, logs)
        self.assertFalse(any("prepared tree" in line for line in logs))

    def test_patches_are_tried_against_the_prepared_tree_without_writing(self):
        self.stamp()
        self.aerie(_patch("url www.9oo91e.qjz9zk", "url www.example.test"))
        status, logs = self.check()
        self.assertEqual(status, 0, logs)
        self.assertTrue(any("prepared tree" in line and "apply" in line for line in logs), logs)
        self.assertEqual((self.tree / "net" / "listed.cc").read_text(), "url www.9oo91e.qjz9zk\n")

    def test_a_patch_that_fits_the_sparse_tree_but_not_the_prepared_tree_fails_the_check(self):
        self.stamp()
        (self.tree / "net" / "listed.cc").write_text("url drifted\n")
        self.aerie(_patch("url www.9oo91e.qjz9zk", "x"))
        status, logs = self.check()
        self.assertEqual(status, 1)
        self.assertIn("prepared tree", "\n".join(logs))

    def test_a_tree_changed_by_an_older_build_is_skipped_with_a_hint(self):
        self.stamp()
        (self.tree / ".aerie-patches.json").write_text("[]")
        self.aerie(_patch("url www.9oo91e.qjz9zk", "x"))
        status, logs = self.check()
        self.assertEqual(status, 0, logs)
        self.assertTrue(any("--clean" in line for line in logs))


if __name__ == "__main__":
    unittest.main()
