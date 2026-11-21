import subprocess
import tempfile
import unittest
from pathlib import Path

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
    def test_a_drifted_patch_is_regenerated_with_its_header_untouched(self):
        (self.tree / "f.txt").write_text("new1\nnew2\n" + lines())
        self.add_patch("a.patch", HEADER + BODY)
        outcome = rebase.refresh_layer(self.tree, self.layer())
        self.assertEqual(outcome.refreshed, ["a.patch"])
        text = (self.pdir / "a.patch").read_text()
        self.assertTrue(text.startswith(HEADER))
        fresh = self.root / "fresh"
        fresh.mkdir()
        (fresh / "f.txt").write_text("new1\nnew2\n" + lines())
        results = patches.apply_layers(fresh, [self.layer()])
        self.assertEqual((results[0].status, results[0].drift), ("ok", ""))
        self.assertEqual((fresh / "f.txt").read_text(), (self.tree / "f.txt").read_text())

    def test_a_patch_that_needed_fuzz_is_refreshed_too(self):
        (self.tree / "f.txt").write_text(lines().replace("line9\n", "other9\n"))
        self.add_patch("a.patch", HEADER + BODY)
        self.assertEqual(rebase.refresh_layer(self.tree, self.layer()).refreshed, ["a.patch"])
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
        outcome = rebase.refresh_layer(self.tree, self.layer())
        self.assertEqual(outcome.refreshed, [])
        for p, m in before.items():
            self.assertEqual((self.pdir / p).stat().st_mtime_ns, m)
        self.assertEqual((self.tree / "g.txt").read_text(), "y\n")

    def test_a_patch_that_really_fails_is_reported_not_refreshed(self):
        (self.tree / "f.txt").write_text("nothing like it\n")
        self.add_patch("a.patch", HEADER + BODY)
        outcome = rebase.refresh_layer(self.tree, self.layer())
        self.assertEqual(outcome.refreshed, [])
        self.assertEqual([r.patch for r in outcome.failed], ["aerie/a.patch"])
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
        return rebase.render_report("0.21.1.1", "0.22.1.1", "157.0.1", "158.0.2", self.rows(), traffic)

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


class RowsTest(Fixture):
    def test_status_and_purpose_come_from_results_and_headers(self):
        (self.tree / "f.txt").write_text("new1\nnew2\n" + lines())
        self.add_patch("a.patch", HEADER + BODY)
        self.add_patch("b.patch", "Aerie-Purpose: B.\n\n--- a/zzz\n+++ b/zzz\n@@ -1 +1 @@\n-a\n+b\n")
        layer = self.layer()
        rows = rebase.rows_for([layer], patches.apply_layers(self.tree, [layer]))
        self.assertEqual([(r.patch, r.status, r.purpose) for r in rows],
                         [("aerie/a.patch", "offset", "Upper-case line ten."), ("aerie/b.patch", "failed", "B.")])

    def test_a_patch_never_tried_is_marked_so(self):
        self.add_patch("a.patch", HEADER + BODY)
        rows = rebase.rows_for([self.layer()], [])
        self.assertEqual(rows[0].status, "not tried")


if __name__ == "__main__":
    unittest.main()
