import io
import subprocess
import tarfile
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from aerie_build import patches, rebase

HEADER = "Aerie-Purpose: Upper-case line ten.\nAerie-Upstream-Area: f.txt\nAerie-Kind: hook\n\nFree text.\n"
BODY = "--- a/f.txt\n+++ b/f.txt\n@@ -9,3 +9,3 @@\n line9\n-line10\n+LINE10\n line11\n"


def lines(n=20, prefix="line"):
    return "".join(f"{prefix}{i}\n" for i in range(1, n + 1))


class Fixture(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.root = Path(self._tmp.name)
        self.tree = self.root / "tree"
        self.tree.mkdir()
        self.pdir = self.root / "p"
        self.pdir.mkdir()

    def tearDown(self):
        self._tmp.cleanup()

    def add_patch(self, name, text):
        (self.pdir / name).write_text(text)
        series = self.pdir / "series"
        old = series.read_text() if series.exists() else ""
        series.write_text(old + name + "\n")

    def layer(self, fuzz=False):
        return patches.Layer("aerie", self.pdir, fuzz=fuzz)


class DriftDetectionTest(Fixture):
    def test_an_offset_fails_and_names_the_patch(self):
        (self.tree / "f.txt").write_text("new1\nnew2\n" + lines())
        self.add_patch("a.patch", HEADER + BODY)
        results = patches.apply_layers(self.tree, [self.layer()])
        self.assertEqual(results[0].status, "failed")
        self.assertEqual(results[0].drift, "offset")
        report, ok = patches.format_report(results)
        self.assertFalse(ok)
        self.assertIn("FAILED  aerie/a.patch", report)

    def test_a_patch_that_needs_fuzz_fails_and_names_the_patch_in_the_strict_layer(self):
        (self.tree / "f.txt").write_text(lines().replace("line9\n", "other9\n"))
        self.add_patch("a.patch", HEADER + BODY)
        report, ok = patches.format_report(patches.apply_layers(self.tree, [self.layer()]))
        self.assertFalse(ok)
        self.assertIn("FAILED  aerie/a.patch", report)

    def test_a_fuzzy_layer_records_the_fuzz_and_the_offset_on_a_passing_patch(self):
        (self.tree / "f.txt").write_text(lines().replace("line9\n", "other9\n"))
        self.add_patch("a.patch", HEADER + BODY)
        self.assertEqual(patches.apply_layers(self.tree, [self.layer(fuzz=True)])[0].drift, "fuzz")

    def test_a_fuzzy_layer_records_an_offset(self):
        (self.tree / "f.txt").write_text("new1\nnew2\n" + lines())
        self.add_patch("a.patch", HEADER + BODY)
        r = patches.apply_layers(self.tree, [self.layer(fuzz=True)])[0]
        self.assertEqual((r.status, r.drift), ("ok", "offset"))

    def test_an_exact_patch_has_no_drift(self):
        (self.tree / "f.txt").write_text(lines())
        self.add_patch("a.patch", HEADER + BODY)
        r = patches.apply_layers(self.tree, [self.layer()])[0]
        self.assertEqual((r.status, r.drift), ("ok", ""))


class RefreshTest(Fixture):
    def refresh(self):
        return patches.refresh_layer(self.tree, self.layer())

    def test_a_drifted_patch_is_regenerated_with_its_header_untouched(self):
        (self.tree / "f.txt").write_text("new1\nnew2\n" + lines())
        self.add_patch("a.patch", HEADER + BODY)
        results = self.refresh()
        self.assertEqual([(r.patch, r.status) for r in results], [("aerie/a.patch", "refreshed")])
        self.assertTrue((self.pdir / "a.patch").read_text().startswith(HEADER))
        fresh = self.root / "fresh"
        fresh.mkdir()
        (fresh / "f.txt").write_text("new1\nnew2\n" + lines())
        results = patches.apply_layers(fresh, [self.layer()])
        self.assertEqual((results[0].status, results[0].drift), ("ok", ""))
        self.assertEqual((fresh / "f.txt").read_text(), (self.tree / "f.txt").read_text())

    def test_a_patch_that_needed_fuzz_is_refreshed_too(self):
        (self.tree / "f.txt").write_text(lines().replace("line9\n", "other9\n"))
        self.add_patch("a.patch", HEADER + BODY)
        self.assertEqual(self.refresh()[0].status, "refreshed")
        fresh = self.root / "fresh"
        fresh.mkdir()
        (fresh / "f.txt").write_text(lines().replace("line9\n", "other9\n"))
        self.assertEqual(patches.apply_layers(fresh, [self.layer()])[0].drift, "")

    def test_untouched_patches_are_not_rewritten(self):
        (self.tree / "f.txt").write_text(lines())
        (self.tree / "g.txt").write_text("x\n")
        self.add_patch("a.patch", HEADER + BODY)
        self.add_patch("b.patch", "Aerie-Purpose: p\n\n--- a/g.txt\n+++ b/g.txt\n@@ -1 +1 @@\n-x\n+y\n")
        before = {p: (self.pdir / p).stat().st_mtime_ns for p in ("a.patch", "b.patch")}
        self.assertEqual({r.status for r in self.refresh()}, {"ok"})
        for p, m in before.items():
            self.assertEqual((self.pdir / p).stat().st_mtime_ns, m)
        self.assertEqual((self.tree / "g.txt").read_text(), "y\n")

    def test_a_patch_that_really_fails_is_reported_and_its_dependents_skipped(self):
        (self.tree / "f.txt").write_text("nothing like it\n")
        self.add_patch("a.patch", HEADER + BODY)
        self.add_patch("b.patch", "Aerie-Purpose: p\n\n" + BODY)
        results = self.refresh()
        self.assertEqual([r.status for r in results], ["failed", "skipped"])
        self.assertEqual((self.pdir / "a.patch").read_text(), HEADER + BODY)

    def refuses(self, extra_header):
        (self.tree / "f.txt").write_text("new1\nnew2\n" + lines())
        text = HEADER + extra_header + BODY
        self.add_patch("a.patch", text)
        results = self.refresh()
        self.assertEqual(results[0].status, "failed", results)
        self.assertIn("not refreshed", results[0].detail)
        self.assertEqual((self.pdir / "a.patch").read_text(), text)

    def test_a_drifted_patch_with_a_mode_change_is_refused_and_left_alone(self):
        self.refuses("diff --git a/f.txt b/f.txt\nold mode 100644\nnew mode 100755\n")

    def test_a_drifted_patch_with_a_rename_is_refused_and_left_alone(self):
        self.refuses("diff --git a/f.txt b/f.txt\nsimilarity index 90%\nrename from f.txt\nrename to g.txt\n")

    def test_a_drifted_patch_with_a_binary_part_is_refused_and_left_alone(self):
        (self.tree / "f.txt").write_text("new1\nnew2\n" + lines())
        text = HEADER + BODY + "Binary files a/x.png and b/x.png differ\n"
        self.add_patch("a.patch", text)
        results = self.refresh()
        self.assertEqual(results[0].status, "failed", results)
        self.assertIn("not refreshed", results[0].detail)
        self.assertEqual((self.pdir / "a.patch").read_text(), text)

    def test_header_prose_that_looks_like_a_git_header_does_not_block_a_refresh(self):
        (self.tree / "f.txt").write_text("new1\nnew2\n" + lines())
        prose = "rename from the old name was considered\nindex abc..def is only prose\nBinary files are not touched\n"
        self.add_patch("a.patch", HEADER + prose + BODY)
        self.assertEqual(self.refresh()[0].status, "refreshed")
        self.assertTrue((self.pdir / "a.patch").read_text().startswith(HEADER + prose))

    def snapshot(self):
        return {p.name: p.read_bytes() for p in sorted(self.pdir.iterdir())}

    def two_drifted(self):
        (self.tree / "f.txt").write_text("new1\nnew2\n" + lines())
        (self.tree / "g.txt").write_text("n1\n" + lines())
        self.add_patch("a.patch", HEADER + BODY)
        self.add_patch("b.patch", HEADER + BODY.replace("f.txt", "g.txt"))
        return self.snapshot()

    def test_a_failure_after_the_first_refreshed_patch_leaves_every_patch_file_as_it_was(self):
        before = self.two_drifted()
        real = patches._unified
        calls = []

        def flaky(*args):
            calls.append(args)
            if len(calls) > 1:
                raise RuntimeError("diff blew up")
            return real(*args)

        with mock.patch.object(patches, "_unified", side_effect=flaky):
            with self.assertRaises(RuntimeError):
                self.refresh()
        self.assertEqual(self.snapshot(), before)

    def test_a_failed_strict_check_leaves_every_patch_file_as_it_was(self):
        before = self.two_drifted()
        bad = "--- a/g.txt\n+++ b/g.txt\n@@ -1,3 +1,3 @@\n zzz\n-line10\n+LINE10\n zzz\n"
        real = patches._unified
        with mock.patch.object(patches, "_unified", side_effect=lambda rel, *a: bad if rel == "g.txt" else real(rel, *a)):
            with self.assertRaises(patches.PatchStateError):
                self.refresh()
        self.assertEqual(self.snapshot(), before)

    def test_a_good_refresh_leaves_no_temp_files_behind(self):
        self.two_drifted()
        self.refresh()
        self.assertEqual(sorted(p.name for p in self.pdir.iterdir()), ["a.patch", "b.patch", "series"])

    def test_a_patch_that_would_end_up_with_no_diff_is_refused_and_the_tree_kept(self):
        (self.tree / "f.txt").write_text("new1\nnew2\n" + lines())
        text = HEADER + "--- a/f.txt\n+++ b/f.txt\n@@ -9,3 +9,3 @@\n line9\n-line10\n+line10\n line11\n"
        self.add_patch("a.patch", text)
        results = self.refresh()
        self.assertEqual(results[0].status, "failed")
        self.assertIn("no diff", results[0].detail)
        self.assertEqual((self.pdir / "a.patch").read_text(), text)
        self.assertEqual((self.tree / "f.txt").read_text(), "new1\nnew2\n" + lines())

    def test_a_refresh_that_does_not_apply_exactly_afterwards_fails_loudly(self):
        (self.tree / "f.txt").write_text("new1\nnew2\n" + lines())
        self.add_patch("a.patch", HEADER + BODY)
        bad = "--- a/f.txt\n+++ b/f.txt\n@@ -1,3 +1,3 @@\n zzz\n-line10\n+LINE10\n zzz\n"
        with mock.patch.object(patches, "_unified", return_value=bad):
            with self.assertRaises(patches.PatchStateError):
                self.refresh()
        self.assertEqual((self.pdir / "a.patch").read_text(), HEADER + BODY)


class ReportTest(unittest.TestCase):
    def rows(self):
        return [
            rebase.PatchRow("aerie/a.patch", "clean", "Do a."),
            rebase.PatchRow("aerie/b.patch", "offset", "Do b."),
            rebase.PatchRow("helium-chromium/c.patch", "fuzz", ""),
            rebase.PatchRow("aerie/d.patch", "failed", "Do d."),
        ]

    def render(self, traffic=None):
        versions = rebase.Versions("0.21.1.1", "0.22.1.1", "157.0.1", "158.0.2")
        return rebase.render_report(versions, self.rows(), traffic)

    def test_lists_every_patch_with_status_and_purpose(self):
        text = self.render()
        self.assertIn("| aerie/a.patch | clean | Do a. |", text)
        self.assertIn("| aerie/b.patch | offset | Do b. |", text)
        self.assertIn("| helium-chromium/c.patch | fuzz |", text)
        self.assertIn("| aerie/d.patch | failed | Do d. |", text)

    def test_names_the_versions_and_counts(self):
        text = self.render()
        self.assertIn("0.21.1.1 -> 0.22.1.1", text)
        self.assertIn("157.0.1 -> 158.0.2", text)
        self.assertIn("1 clean, 1 offset, 1 fuzz, 1 failed", text)

    def test_says_plainly_when_the_traffic_diff_was_not_run_and_how_to_run_it(self):
        text = self.render()
        self.assertIn("not run", text)
        self.assertIn("./aerie traffic-diff --old-tree <old tree> --new-tree <new tree>", text)

    def test_lists_added_and_removed_annotations(self):
        text = self.render(rebase.TrafficDiff(added=["new_one"], removed=["gone_one"]))
        self.assertIn("`new_one`", text)
        self.assertIn("`gone_one`", text)
        self.assertIn("docs/network-endpoints.md", text)
        self.assertNotIn("not run", text)

    def test_an_empty_traffic_diff_says_nothing_changed(self):
        self.assertIn("No annotations were added or removed", self.render(rebase.TrafficDiff([], [])))


class TrafficTest(unittest.TestCase):
    SRC = '''
net::NetworkTrafficAnnotationTag t = net::DefineNetworkTrafficAnnotation("one", R"(x)");
auto p = net::DefinePartialNetworkTrafficAnnotation(
    "two_partial", "completing", R"(y)");
auto c = net::CompleteNetworkTrafficAnnotation("three", p, R"(z)");
auto b = net::BranchedCompleteNetworkTrafficAnnotation("four", "four_partial", p, R"(z)");
auto n = NO_TRAFFIC_ANNOTATION_YET;
'''

    def make(self, root, files):
        for rel, text in files.items():
            path = Path(root) / rel
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text)

    def test_reads_the_id_of_every_annotation_form(self):
        with tempfile.TemporaryDirectory() as tmp:
            self.make(tmp, {"a/b.cc": self.SRC, "a/readme.md": 'DefineNetworkTrafficAnnotation("skip", "")'})
            self.assertEqual(rebase.traffic_ids(Path(tmp)), {"one", "two_partial", "three", "four"})

    def test_diffs_two_trees(self):
        with tempfile.TemporaryDirectory() as old, tempfile.TemporaryDirectory() as new:
            self.make(old, {"a.cc": self.SRC})
            self.make(new, {"a.cc": self.SRC.replace('"one"', '"uno"'),
                            "sub/b.h": 'x = net::DefineNetworkTrafficAnnotation("fresh", "")'})
            diff = rebase.diff_traffic(rebase.traffic_ids(Path(old)), rebase.traffic_ids(Path(new)))
            self.assertEqual(diff.added, ["fresh", "uno"])
            self.assertEqual(diff.removed, ["one"])


class TestPathTest(unittest.TestCase):
    def test_test_only_paths(self):
        for path in ("chrome/test/a.cc", "x/tests/a.cc", "base/testing/a.cc", "x/test_support/a.cc",
                     "x/foo_test.cc", "x/foo_unittest.cc", "x/foo_browsertest.cc", "x/foo_test_util.h",
                     "x/fake_thing.cc", "x/mock_thing.h", "chromium-1/chrome/test/a.cc"):
            self.assertTrue(rebase.is_test_path(path), path)

    def test_production_paths(self):
        for path in ("chrome/browser/a.cc", "x/latest.cc", "x/testimony.cc", "x/contest/a.cc", "x/fakery.cc",
                     "x/test.cc"):
            self.assertFalse(rebase.is_test_path(path), path)

    def test_traffic_in_test_files_is_left_out(self):
        src = 'x = net::DefineNetworkTrafficAnnotation("%s", "")'
        with tempfile.TemporaryDirectory() as tmp:
            for rel, name in (("a/real.cc", "real"), ("a/test/t.cc", "t1"), ("a/x_unittest.cc", "t2"),
                              ("a/fake_x.cc", "t3")):
                path = Path(tmp) / rel
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(src % name)
            self.assertEqual(rebase.traffic_ids(Path(tmp)), {"real"})


class ArchiveTest(unittest.TestCase):
    FILES = {
        "chromium-1/a/real.cc": 'net::DefineNetworkTrafficAnnotation("real", "")',
        "chromium-1/a/real.h": 'net::DefineNetworkTrafficAnnotation("in_header", "")',
        "chromium-1/a/test/skip.cc": 'net::DefineNetworkTrafficAnnotation("test_one", "")',
        "chromium-1/a/notes.txt": 'net::DefineNetworkTrafficAnnotation("text_file", "")',
    }

    def make_tar(self, root: Path) -> Path:
        plain = root / "c.tar"
        with tarfile.open(plain, "w") as tf:
            for name, text in self.FILES.items():
                data = text.encode()
                info = tarfile.TarInfo(name)
                info.size = len(data)
                tf.addfile(info, io.BytesIO(data))
        return plain

    def test_reads_ids_from_a_tar_zst_without_extracting_it(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            plain = self.make_tar(root)
            subprocess.run(["zstd", "-q", str(plain), "-o", str(root / "c.tar.zst")], check=True)
            before = set(root.iterdir())
            self.assertEqual(rebase.traffic_ids_in_archive(root / "c.tar.zst"), {"real", "in_header"})
            self.assertEqual(set(root.iterdir()), before)

    def test_reads_ids_from_a_gzip_tar(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            plain = self.make_tar(root)
            subprocess.run(["gzip", "-k", str(plain)], check=True)
            self.assertEqual(rebase.traffic_ids_in_archive(root / "c.tar.gz"), {"real", "in_header"})

    def test_a_damaged_zst_is_an_error(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            plain = self.make_tar(root)
            subprocess.run(["zstd", "-q", str(plain), "-o", str(root / "c.tar.zst")], check=True)
            data = (root / "c.tar.zst").read_bytes()
            (root / "bad.tar.zst").write_bytes(data[:len(data) // 2])
            with self.assertRaises(RuntimeError) as ctx:
                rebase.traffic_ids_in_archive(root / "bad.tar.zst")
            self.assertIn("zstd", str(ctx.exception))

    def test_a_member_over_the_size_cap_is_named_in_a_warning(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            plain = self.make_tar(root)
            warnings = []
            with mock.patch.object(rebase, "_MAX_MEMBER_BYTES", 10):
                ids = rebase.traffic_ids_in_archive(plain, log=warnings.append)
            self.assertEqual(ids, set())
            self.assertTrue(any("chromium-1/a/real.cc" in w for w in warnings), warnings)
            self.assertFalse(any("skip.cc" in w or "notes.txt" in w for w in warnings), warnings)

    def test_zstd_is_closed_and_reaped_when_the_stream_is_cut_short(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            plain = self.make_tar(root)
            subprocess.run(["zstd", "-q", str(plain), "-o", str(root / "c.tar.zst")], check=True)
            made = []
            real = subprocess.Popen

            def spy(*a, **k):
                proc = real(*a, **k)
                made.append(proc)
                return proc

            with mock.patch.object(rebase.subprocess, "Popen", side_effect=spy):
                self.assertEqual(rebase.traffic_ids_in_archive(root / "c.tar.zst"), {"real", "in_header"})
            self.assertIsNotNone(made[0].returncode)
            self.assertTrue(made[0].stdout.closed and made[0].stderr.closed)

    def test_computing_the_diff_reports_a_reason_instead_of_raising(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            plain = self.make_tar(root)
            subprocess.run(["zstd", "-q", str(plain), "-o", str(root / "c.tar.zst")], check=True)
            data = (root / "c.tar.zst").read_bytes()
            (root / "bad.tar.zst").write_bytes(data[:len(data) // 2])
            diff, reason = rebase.compute_traffic([root / "bad.tar.zst"], [plain], lambda m: None)
            self.assertIsNone(diff)
            self.assertIn("zstd", reason)
            with mock.patch.object(rebase.subprocess, "Popen", side_effect=FileNotFoundError("zstd")):
                diff, reason = rebase.compute_traffic([root / "c.tar.zst"], [plain], lambda m: None)
            self.assertIsNone(diff)
            self.assertIn("zstd", reason)
            diff, reason = rebase.compute_traffic([root / "nope"], [plain], lambda m: None)
            self.assertIsNone(diff)
            self.assertIn("old release", reason)
            diff, reason = rebase.compute_traffic([plain], [plain], lambda m: None)
            self.assertEqual((diff, reason), (rebase.TrafficDiff([], []), ""))

    def test_the_report_says_why_the_diff_was_not_run(self):
        text = rebase.render_report(rebase.Versions("1", "2", "3", "4"), [], None, "zstd could not decompress x")
        self.assertIn("not run", text)
        self.assertIn("zstd could not decompress x", text)
        self.assertIn(rebase.TRAFFIC_COMMAND, text)

    def test_side_ids_uses_the_first_source_that_exists_and_says_none_otherwise(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            tree = root / "tree"
            tree.mkdir()
            (tree / "a.cc").write_text('net::DefineNetworkTrafficAnnotation("from_tree", "")')
            plain = self.make_tar(root)
            self.assertEqual(rebase.side_ids(None, root / "missing.tar.zst", tree), {"from_tree"})
            self.assertEqual(rebase.side_ids(None, plain), {"real", "in_header"})
            self.assertIsNone(rebase.side_ids(None, root / "missing"))


class KeepTrafficSectionTest(unittest.TestCase):
    VERSIONS = rebase.Versions("1", "2", "3", "4")

    def test_a_rerun_without_a_diff_keeps_the_section_filled_in_before(self):
        filled = rebase.render_report(self.VERSIONS, [rebase.PatchRow("aerie/a", "clean", "A")],
                                      rebase.TrafficDiff(["new_one"], []))
        rerun = rebase.render_report(self.VERSIONS, [rebase.PatchRow("aerie/a", "offset", "A")], None)
        merged = rebase.keep_traffic_section(rerun, filled)
        self.assertIn("`new_one`", merged)
        self.assertNotIn("not run", merged)
        self.assertIn("| aerie/a | offset | A |", merged)  # the patch table is the new run's

    def test_a_new_diff_replaces_the_old_section(self):
        old = rebase.render_report(self.VERSIONS, [], rebase.TrafficDiff(["old_one"], []))
        new = rebase.render_report(self.VERSIONS, [], rebase.TrafficDiff(["newer"], []))
        merged = rebase.keep_traffic_section(new, old)
        self.assertIn("`newer`", merged)
        self.assertNotIn("old_one", merged)

    def test_nothing_to_keep_returns_the_new_report(self):
        rerun = rebase.render_report(self.VERSIONS, [], None)
        self.assertEqual(rebase.keep_traffic_section(rerun, None), rerun)
        self.assertEqual(rebase.keep_traffic_section(rerun, rerun), rerun)


class RowsTest(Fixture):
    def test_status_and_purpose_come_from_results_and_headers(self):
        (self.tree / "f.txt").write_text("new1\nnew2\n" + lines())
        self.add_patch("a.patch", HEADER + BODY)
        self.add_patch("b.patch", "Aerie-Purpose: B.\n\n--- a/zzz\n+++ b/zzz\n@@ -1 +1 @@\n-a\n+b\n")
        layer = self.layer()
        rows = rebase.rows_for([layer], patches.apply_layers(self.tree, [layer]))
        self.assertEqual([(r.patch, r.status, r.purpose) for r in rows],
                         [("aerie/a.patch", "offset", "Upper-case line ten."), ("aerie/b.patch", "failed", "B.")])

    def test_the_status_comes_from_the_drift_field_not_from_the_message(self):
        layer = self.layer()
        self.add_patch("a.patch", HEADER + BODY)
        self.add_patch("b.patch", HEADER + BODY)
        results = [patches.PatchResult("aerie/a.patch", "failed", "whatever", "fuzz"),
                   patches.PatchResult("aerie/b.patch", "failed", "the context has drifted", "")]
        self.assertEqual([r.status for r in rebase.rows_for([layer], results)], ["fuzz", "failed"])

    def test_a_patch_never_tried_is_marked_so(self):
        self.add_patch("a.patch", HEADER + BODY)
        rows = rebase.rows_for([self.layer()], [])
        self.assertEqual(rows[0].status, "not tried")


if __name__ == "__main__":
    unittest.main()
