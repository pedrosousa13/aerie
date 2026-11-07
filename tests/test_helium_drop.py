"""patches/helium-drop.txt: Helium patches that Aerie does not apply."""

import tempfile
import unittest
from pathlib import Path

from aerie_build import patch_check, patches

REPO = Path(__file__).resolve().parents[1]


def write(path: Path, text: str) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)
    return path


class ParseDropsTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.dir = Path(self._tmp.name)

    def parse(self, text):
        return patches.parse_drops(write(self.dir / "helium-drop.txt", text))

    def test_entries_with_reasons(self):
        drops, problems = self.parse("# header\n\nhelium/core/a.patch  # why a\nhelium/ui/b.patch # why b\n")
        self.assertEqual(problems, [])
        self.assertEqual([(d.entry, d.reason) for d in drops],
                         [("helium/core/a.patch", "why a"), ("helium/ui/b.patch", "why b")])

    def test_a_reason_is_required(self):
        for text in ("helium/a.patch\n", "helium/a.patch #\n", "helium/a.patch #   \n"):
            drops, problems = self.parse(text)
            self.assertEqual(drops, [], text)
            self.assertIn("needs a reason", problems[0].message)

    def test_bad_entries(self):
        for text in ("../x.patch # r\n", "/abs.patch # r\n", "two words.patch # r\n", "# only a comment but then\n  # r\n"):
            drops, problems = self.parse(text)
            if text.startswith("#"):
                self.assertEqual((drops, problems), ([], []))
            else:
                self.assertTrue(problems, text)

    def test_duplicates(self):
        _, problems = self.parse("helium/a.patch # r\nhelium/a.patch # again\n")
        self.assertIn("more than once", problems[0].message)

    def test_missing_file_means_no_drops(self):
        self.assertEqual(patches.parse_drops(self.dir / "nope.txt"), ([], []))


class LayerDropTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.dir = Path(self._tmp.name) / "patches"
        write(self.dir / "series", "# c\nhelium/a.patch\nhelium/b.patch\nhelium/c.patch\n")
        for name, old, new in (("a", "A", "A2"), ("b", "B", "B2"), ("c", "C", "C2")):
            write(self.dir / f"helium/{name}.patch",
                  f"--- a/{name}.txt\n+++ b/{name}.txt\n@@ -1 +1 @@\n-{old}\n+{new}\n")
        self.layer = patches.Layer("helium-chromium", self.dir, fuzz=True)

    def test_dropped_entries_leave_every_view_of_the_series(self):
        layer = patches.with_drops(self.layer, [patches.Drop("helium/b.patch", "r")])
        self.assertEqual(patches.layer_series(layer), ["helium/a.patch", "helium/c.patch"])
        self.assertEqual([n for n, _ in patches.layer_entries(layer)],
                         [f"{self.layer.label}/helium/a.patch", f"{self.layer.label}/helium/c.patch"])
        self.assertNotIn("b.txt", patches.wanted_files([layer]))

    def test_drops_for_another_layer_are_ignored(self):
        layer = patches.with_drops(self.layer, [patches.Drop("helium/elsewhere.patch", "r")])
        self.assertEqual(layer.dropped, frozenset())

    def test_apply_skips_dropped_patches(self):
        tree = Path(self._tmp.name) / "tree"
        for name, text in (("a", "A\n"), ("b", "B\n"), ("c", "C\n")):
            write(tree / f"{name}.txt", text)
        layer = patches.with_drops(self.layer, [patches.Drop("helium/b.patch", "r")])
        results = patches.apply_layers(tree, [layer])
        self.assertEqual([r.status for r in results], ["ok", "ok"])
        self.assertEqual((tree / "a.txt").read_text(), "A2\n")
        self.assertEqual((tree / "b.txt").read_text(), "B\n")   # untouched
        self.assertEqual((tree / "c.txt").read_text(), "C2\n")

    def test_a_patch_that_needs_a_dropped_one_fails_and_says_so(self):
        # c edits what b would have changed; without b it cannot apply.
        write(self.dir / "helium/c.patch", "--- a/b.txt\n+++ b/b.txt\n@@ -1 +1 @@\n-B2\n+B3\n")
        tree = Path(self._tmp.name) / "tree"
        write(tree / "a.txt", "A\n")
        write(tree / "b.txt", "B\n")
        layer = patches.with_drops(self.layer, [patches.Drop("helium/b.patch", "r")])
        results = patches.apply_layers(tree, [layer])
        self.assertEqual([r.status for r in results], ["ok", "failed"])

    def test_filtered_series_dir_is_something_heliums_applier_can_read(self):
        layer = patches.with_drops(self.layer, [patches.Drop("helium/b.patch", "r")])
        out = patches.filtered_series_dir(layer, Path(self._tmp.name) / "filtered")
        self.assertEqual(patches.parse_series(out / "series"), ["helium/a.patch", "helium/c.patch"])
        self.assertTrue((out / "helium/a.patch").is_file())      # a symlink to the real file
        self.assertTrue((out / "helium/a.patch").is_symlink())
        self.assertFalse((out / "helium/b.patch").exists())
        # Rebuilding it clears the old content.
        out2 = patches.filtered_series_dir(self.layer, out)
        self.assertTrue((out2 / "helium/b.patch").exists())


class CheckDropsTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        root = Path(self._tmp.name)
        write(root / "core/series", "helium/a.patch\n")
        write(root / "core/helium/a.patch", "x")
        write(root / "linux/series", "linux/l.patch\nlinux/missing.patch\n")
        write(root / "linux/linux/l.patch", "x")
        self.layers = [patches.Layer("helium-chromium", root / "core", True),
                       patches.Layer("helium-linux", root / "linux", True)]

    def test_drops_in_either_helium_series_pass(self):
        self.assertEqual(patches.check_drops([patches.Drop("helium/a.patch", "r"),
                                              patches.Drop("linux/l.patch", "r")], self.layers), [])

    def test_a_drop_that_left_helium_fails_so_a_bump_surfaces_it(self):
        problems = patches.check_drops([patches.Drop("helium/gone.patch", "r")], self.layers)
        self.assertEqual(len(problems), 1)
        self.assertIn("no longer in Helium's series", problems[0].message)

    def test_a_drop_whose_file_is_missing_fails(self):
        problems = patches.check_drops([patches.Drop("linux/missing.patch", "r")], self.layers)
        self.assertIn("file is missing", problems[0].message)


HEADER = "Aerie-Purpose: p\nAerie-Upstream-Area: a\nAerie-Kind: hook\n\n"


class AdaptTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        root = Path(self._tmp.name)
        self.helium = root / "helium"
        write(self.helium / "series", "helium/a.patch\nhelium/b.patch\n")
        write(self.helium / "helium/a.patch", "--- a/a.txt\n+++ b/a.txt\n@@ -1 +1 @@\n-A\n+A2\n")
        write(self.helium / "helium/b.patch", "--- a/b.txt\n+++ b/b.txt\n@@ -1 +1 @@\n-B\n+B2\n")
        self.aerie = root / "patches"
        self.layer = patches.Layer("helium-chromium", self.helium, fuzz=True)

    def helium_sha(self, entry="helium/b.patch"):
        import hashlib
        return hashlib.sha256((self.helium / entry).read_bytes()).hexdigest()

    def adapt(self, entry="helium/b.patch", body=None, listed=True, sha=None):
        if listed:
            pin = sha if sha is not None else (self.helium_sha(entry) if (self.helium / entry).exists() else "0" * 64)
            write(self.aerie / "helium-adapt.txt", f"{entry}  sha256:{pin}  # because\n")
        if body is not None:
            write(self.aerie / "helium-adapted" / entry, body)
        return patches.parse_adapts(self.aerie)

    def test_replacement_takes_the_place_in_the_series(self):
        adapts, problems = self.adapt(body=HEADER + "--- a/b.txt\n+++ b/b.txt\n@@ -1 +1 @@\n-B\n+B3\n")
        self.assertEqual(problems, [])  # (the content comparison is check_adapts' job)
        layer = patches.with_drops(self.layer, [], adapts)
        entries = patches.layer_entries(layer)
        self.assertEqual([n for n, _ in entries], [f"{self.layer.label}/helium/a.patch", f"{self.layer.label}/helium/b.patch"])
        self.assertEqual(entries[1][1], self.aerie / "helium-adapted/helium/b.patch")
        tree = Path(self._tmp.name) / "tree"
        write(tree / "a.txt", "A\n")
        write(tree / "b.txt", "B\n")
        self.assertEqual([r.status for r in patches.apply_layers(tree, [layer])], ["ok", "ok"])
        self.assertEqual((tree / "b.txt").read_text(), "B3\n")  # Aerie's version, not Helium's

    def test_filtered_dir_links_the_replacement(self):
        adapts, _ = self.adapt(body=HEADER + "x")
        layer = patches.with_drops(self.layer, [], adapts)
        out = patches.filtered_series_dir(layer, Path(self._tmp.name) / "out")
        self.assertEqual((out / "helium/b.patch").read_text(), HEADER + "x")
        self.assertEqual((out / "helium/a.patch").resolve(), (self.helium / "helium/a.patch").resolve())

    def test_a_listed_entry_needs_its_replacement_file(self):
        _, problems = self.adapt(body=None)
        self.assertIn("does not exist", problems[0].message)

    def test_the_replacement_needs_the_aerie_header(self):
        _, problems = self.adapt(body="--- a/b.txt\n+++ b/b.txt\n")
        self.assertTrue(any("Aerie-Purpose" in p.message for p in problems))

    def test_an_entry_that_left_helium_is_reported(self):
        adapts, _ = self.adapt(entry="helium/gone.patch", body=HEADER)
        problems = patches.check_adapts(adapts, [], [self.layer])
        self.assertIn("no longer in Helium's series", problems[0].message)

    def test_an_entry_cannot_be_dropped_and_adapted(self):
        adapts, _ = self.adapt(body=HEADER)
        problems = patches.check_adapts(adapts, [patches.Drop("helium/b.patch", "r")], [self.layer])
        self.assertTrue(any("both" in p.message for p in problems))

    def test_reason_is_required_here_too(self):
        write(self.aerie / "helium-adapt.txt", f"helium/b.patch sha256:{'0' * 64}\n")
        _, problems = patches.parse_adapts(self.aerie)
        self.assertIn("needs a reason", problems[0].message)

    # Drift: the Helium patch that was adapted is pinned by sha256.

    def test_a_pin_is_required_and_must_look_like_a_sha256(self):
        for line in ("helium/b.patch  # because\n", "helium/b.patch sha256:xyz  # because\n"):
            write(self.aerie / "helium-adapt.txt", line)
            write(self.aerie / "helium-adapted/helium/b.patch", HEADER)
            _, problems = patches.parse_adapts(self.aerie)
            self.assertTrue(any("sha256" in p.message for p in problems), line)

    def same_body(self, entry="helium/b.patch"):
        return HEADER + (self.helium / entry).read_text()

    def test_a_pin_that_matches_helium_passes(self):
        adapts, _ = self.adapt(body=self.same_body())
        self.assertEqual(patches.check_adapts(adapts, [], [self.layer]), [])

    def test_a_helium_bump_that_changes_the_patch_fails_the_check(self):
        adapts, _ = self.adapt(body=self.same_body())
        write(self.helium / "helium/b.patch", "--- a/b.txt\n+++ b/b.txt\n@@ -1 +1 @@\n-B\n+B9\n")
        problems = patches.check_adapts(adapts, [], [self.layer])
        self.assertIn("changed since it was adapted", problems[0].message)
        self.assertIn("re-cut", problems[0].message)

    # The re-cut may only move hunks, never change what the patch adds.

    ORIGINAL = ("--- a/x.txt\n+++ b/x.txt\n@@ -1,2 +1,3 @@\n ctx\n+added one\n ctx2\n"
                "--- a/y.txt\n+++ b/y.txt\n@@ -5,2 +5,3 @@\n ctx UBlockThing\n+added two\n ctx3\n"
                "--- a/z.txt\n+++ b/z.txt\n@@ -1 +1 @@\n-old\n+new\n")

    def test_a_recut_with_the_same_added_and_removed_lines_matches(self):
        recut = (HEADER + "--- a/x.txt\n+++ b/x.txt\n@@ -9,2 +9,3 @@\n other\n+added one\n ctx2\n"
                 "--- a/y.txt\n+++ b/y.txt\n@@ -1,2 +1,3 @@\n different context\n+added two\n ctx3\n"
                 "--- a/z.txt\n+++ b/z.txt\n@@ -1 +1 @@\n-old\n+new\n")
        self.assertEqual(patches.recut_problems(self.ORIGINAL, recut), [])

    def test_an_invented_line_is_caught(self):
        recut = self.ORIGINAL + "+\n+sneaky\n"
        problems = patches.recut_problems(self.ORIGINAL, recut)
        self.assertTrue(any("not in Helium's patch" in p for p in problems), problems)

    def test_a_lost_line_is_caught(self):
        recut = self.ORIGINAL.replace("+added one\n", "")
        problems = patches.recut_problems(self.ORIGINAL, recut)
        self.assertTrue(any("is missing from the re-cut" in p for p in problems), problems)

    def test_a_changed_line_is_caught(self):
        recut = self.ORIGINAL.replace("+new", "+newer")
        self.assertTrue(patches.recut_problems(self.ORIGINAL, recut))

    def test_a_ublock_mention_in_the_recut_is_caught(self):
        recut = self.ORIGINAL.replace("+added two", "+added two for uBO")
        self.assertTrue(patches.recut_problems(self.ORIGINAL.replace("+added two", "+added two for uBO"), recut))

    def test_check_adapts_runs_the_line_comparison(self):
        self.adapt(body=HEADER + "--- a/b.txt\n+++ b/b.txt\n@@ -1 +1 @@\n-B\n+B3\n")  # Helium's adds B2
        adapts, _ = patches.parse_adapts(self.aerie)
        problems = patches.check_adapts(adapts, [], [self.layer])
        self.assertTrue(any("not in Helium's patch" in p.message for p in problems), problems)

    def test_replacements_are_not_flagged_as_unlisted_aerie_patches(self):
        write(self.aerie / "series", "")
        self.adapt(body=HEADER)
        self.assertEqual(patches.lint_patch_dir(self.aerie), [])


class RepoDropsTest(unittest.TestCase):
    def test_no_remaining_helium_patch_mentions_ublock_origin(self):
        # If a Helium bump adds a patch that needs the dropped uBlock Origin code, this says so.
        import re
        layers = patch_check.layers(REPO)[:2]
        pattern = re.compile(r"ublock|\bubo\b|blockjmkbacgjkknlgpkjjiijinjdanf", re.I)
        for layer in layers:
            for name, path in patches.layer_entries(layer):
                text = path.read_text(errors="replace")
                body = text[re.search(r"(?m)^--- ", text).start():] if re.search(r"(?m)^--- ", text) else text  # not Aerie's header
                self.assertIsNone(pattern.search(body), f"{name} still mentions uBlock Origin")
        # (a match needs a word boundary or the whole word, so "MenuBlock..." does not trigger it)

    def test_the_adapted_patches_are_listed_and_have_ledger_rows(self):
        adapts, problems = patches.parse_adapts(REPO / "patches")
        self.assertEqual(problems, [])
        self.assertEqual(sorted(a.entry for a in adapts),
                         ["helium/core/component-updates.patch", "helium/core/fixups-component-setup.patch"])
        ledger = (REPO / "docs" / "helium-dependency-ledger.md").read_text()
        for adapt in adapts:
            self.assertIn(f"`helium-chromium/patches/{adapt.entry}`", ledger)

    def test_the_six_ublock_patches_are_dropped(self):
        drops, _ = patch_check.drop_problems(REPO)
        self.assertEqual(sorted(d.entry for d in drops), sorted([
            "helium/core/ublock-setup-sources.patch", "helium/core/ublock-install-as-component.patch",
            "helium/core/ublock-reconfigure-defaults.patch", "helium/core/ublock-helium-services.patch",
            "helium/core/ublock-dns-uncloaking.patch", "helium/ui/ublock-show-in-settings.patch"]))

    def test_the_recut_method_the_adapted_headers_point_to_is_written_down(self):
        contributing = (REPO / "CONTRIBUTING.md").read_text()
        self.assertIn("Re-cutting an adapted patch", contributing)
        adapts, _ = patches.parse_adapts(REPO / "patches")
        for adapt in adapts:
            self.assertIn("Re-cutting an adapted patch", adapt.path.read_text())

    def test_mv2_support_is_not_dropped(self):
        entries = {e for layer in patch_check.layers(REPO)[:2] for e in patches.layer_series(layer)}
        self.assertIn("ungoogled-chromium/extensions-manifestv2.patch", entries)

    def test_the_repos_drop_file_is_valid_against_the_pinned_helium(self):
        drops, problems = patch_check.drop_problems(REPO)
        self.assertEqual(problems, [])

    def test_every_drop_has_a_ledger_row(self):
        drops, _ = patch_check.drop_problems(REPO)
        ledger = (REPO / "docs" / "helium-dependency-ledger.md").read_text()
        for drop in drops:
            self.assertIn(f"`helium-chromium/patches/{drop.entry}`", ledger, f"ledger has no row for {drop.entry}")

    def test_layers_skip_the_dropped_entries(self):
        drops, _ = patch_check.drop_problems(REPO)
        helium = patch_check.layers(REPO)[:2]
        entries = {e for layer in helium for e in patches.layer_series(layer)}
        for drop in drops:
            self.assertNotIn(drop.entry, entries)


if __name__ == "__main__":
    unittest.main()
