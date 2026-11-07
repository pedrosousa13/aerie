import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from aerie_build import patches

GOOD_HEADER = """Aerie-Purpose: Show the Aerie product name.
Aerie-Upstream-Area: chrome/app/theme/chromium/BRANDING
Aerie-Kind: branding

"""


def diff(path: str, old: str, new: str) -> str:
    return (
        f"--- a/{path}\n+++ b/{path}\n@@ -1 +1 @@\n-{old}\n+{new}\n"
    )


class SeriesTest(unittest.TestCase):
    def test_parse_series_skips_blanks_and_comments(self):
        with tempfile.TemporaryDirectory() as tmp:
            series = Path(tmp) / "series"
            series.write_text("# c\n\na/one.patch\nb/two.patch # trailing\n")
            self.assertEqual(patches.parse_series(series), ["a/one.patch", "b/two.patch"])


class HeaderTest(unittest.TestCase):
    def test_parses_fields_before_the_diff(self):
        header = patches.parse_header(GOOD_HEADER + diff("f", "a", "b"))
        self.assertEqual(header["Aerie-Purpose"], "Show the Aerie product name.")
        self.assertEqual(header["Aerie-Upstream-Area"], "chrome/app/theme/chromium/BRANDING")
        self.assertEqual(header["Aerie-Kind"], "branding")

    def test_continuation_lines_join(self):
        text = "Aerie-Purpose: one\n  two\nAerie-Upstream-Area: x\n" + diff("f", "a", "b")
        self.assertEqual(patches.parse_header(text)["Aerie-Purpose"], "one two")

    def test_fields_inside_the_diff_do_not_count(self):
        text = diff("f", "a", "b") + "Aerie-Purpose: sneaky\n"
        self.assertEqual(patches.parse_header(text), {})


class TouchedFilesTest(unittest.TestCase):
    def test_lists_files_once_without_prefix(self):
        text = diff("a/b.cc", "x", "y") + diff("c.h", "x", "y")
        self.assertEqual(patches.touched_files(text), ["a/b.cc", "c.h"])

    def test_new_and_deleted_files(self):
        text = "--- /dev/null\n+++ b/new.txt\n@@ -0,0 +1 @@\n+x\n" \
               "--- a/gone.txt\n+++ /dev/null\n@@ -1 +0,0 @@\n-x\n"
        self.assertEqual(patches.touched_files(text), ["new.txt", "gone.txt"])


class LintTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self._tmp.name)

    def tearDown(self):
        self._tmp.cleanup()

    def write(self, name: str, text: str):
        path = self.dir / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)

    def series(self, *names: str):
        (self.dir / "series").write_text("\n".join(names) + "\n")

    def lint(self):
        return [(p.patch, p.message) for p in patches.lint_patch_dir(self.dir)]

    def test_clean_directory_has_no_problems(self):
        self.write("a/one.patch", GOOD_HEADER + diff("f", "a", "b"))
        self.series("a/one.patch")
        self.assertEqual(self.lint(), [])

    def test_missing_series_file(self):
        self.assertEqual([m for _, m in self.lint()], ["series file is missing"])

    def test_listed_but_absent_patch(self):
        self.series("a/gone.patch")
        problems = self.lint()
        self.assertEqual(problems[0][0], "a/gone.patch")
        self.assertIn("does not exist", problems[0][1])

    def test_unlisted_patch_file(self):
        self.write("a/one.patch", GOOD_HEADER + diff("f", "a", "b"))
        self.write("a/stray.patch", GOOD_HEADER + diff("f", "a", "b"))
        self.series("a/one.patch")
        problems = self.lint()
        self.assertEqual(len(problems), 1)
        self.assertEqual(problems[0][0], "a/stray.patch")
        self.assertIn("not listed in series", problems[0][1])

    def test_duplicate_entry(self):
        self.write("a/one.patch", GOOD_HEADER + diff("f", "a", "b"))
        self.series("a/one.patch", "a/one.patch")
        self.assertIn("listed more than once", self.lint()[0][1])

    def test_missing_header_names_patch_and_field(self):
        self.write("a/one.patch", diff("f", "a", "b"))
        self.series("a/one.patch")
        messages = [m for p, m in self.lint() if p == "a/one.patch"]
        self.assertEqual(len(messages), 2)
        self.assertTrue(any("Aerie-Purpose" in m for m in messages))
        self.assertTrue(any("Aerie-Upstream-Area" in m for m in messages))

    def test_empty_field_counts_as_missing(self):
        self.write("a/one.patch", "Aerie-Purpose:   \nAerie-Upstream-Area: x\n" + diff("f", "a", "b"))
        self.series("a/one.patch")
        problems = self.lint()
        self.assertEqual(len(problems), 1)
        self.assertIn("Aerie-Purpose", problems[0][1])

    def test_unknown_kind_is_rejected(self):
        self.write("a/one.patch", GOOD_HEADER.replace("branding", "magic") + diff("f", "a", "b"))
        self.series("a/one.patch")
        self.assertIn("Aerie-Kind", self.lint()[0][1])

    def test_path_traversal_and_absolute_paths_are_rejected(self):
        for bad in ("../outside", "/etc/passwd", "a/../../b"):
            with self.subTest(bad=bad):
                self.write("a/one.patch", GOOD_HEADER + f"--- a/x\n+++ b/{bad}\n@@ -1 +1 @@\n-a\n+b\n")
                self.series("a/one.patch")
                self.assertTrue(any("unsafe path" in m for _, m in self.lint()))

    def test_series_entry_cannot_escape_the_directory(self):
        self.series("../evil.patch")
        self.assertTrue(any("outside the patch directory" in m for _, m in self.lint()))


class SubstitutionLintTest(unittest.TestCase):
    """Aerie patches apply after Helium's substitutions, so added lines must already be final."""

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self._tmp.name) / "patches"
        self.dir.mkdir()
        core = Path(self._tmp.name) / "core"
        core.mkdir()
        (core / "domain_regex.list").write_text(r"google([A-Za-z\-]*?\\*?)\.com#9oo91e\g<1>.qjz9zk" + "\n")
        (core / "domain_substitution.list").write_text("net/listed.cc\n")
        self.rules = patches.load_substitution_rules(core)

    def tearDown(self):
        self._tmp.cleanup()

    def lint(self, body: str) -> list[str]:
        (self.dir / "a.patch").write_text(GOOD_HEADER + body)
        (self.dir / "series").write_text("a.patch\n")
        return [p.message for p in patches.lint_patch_dir(self.dir, self.rules)]

    def test_added_line_with_a_domain_in_a_domain_substituted_file_is_rejected(self):
        problems = self.lint(diff("net/listed.cc", "x", 'url = "https://www.google.com/x";'))
        self.assertEqual(len(problems), 1)
        self.assertIn("domain_regex.list", problems[0])

    def test_the_same_line_in_a_file_the_substitution_skips_is_fine(self):
        self.assertEqual(self.lint(diff("net/other.cc", "x", 'url = "https://www.google.com/x";')), [])

    def test_removed_and_context_lines_are_not_judged(self):
        body = "--- a/net/listed.cc\n+++ b/net/listed.cc\n@@ -1,2 +1,2 @@\n www.google.com\n-www.google.com/old\n+fine\n"
        self.assertEqual(self.lint(body), [])

    def test_added_chrome_or_chromium_in_a_grd_line_is_rejected(self):
        for ext in ("grd", "grdp"):
            for word in ("Chrome", "Chromium"):
                problems = self.lint(diff(f"c/s.{ext}", "x", f"  Open {word} now"))
                self.assertEqual(len(problems), 1, (ext, word))
                self.assertIn("name substitution", problems[0])

    def test_chrome_in_a_code_file_or_inside_a_longer_word_is_fine(self):
        self.assertEqual(self.lint(diff("c/x.cc", "x", "Chrome")), [])
        self.assertEqual(self.lint(diff("c/s.grd", "x", "Chromeless")), [])

    def test_patches_may_not_edit_translation_files(self):
        problems = self.lint(diff("c/s_de.xtb", "x", "y"))
        self.assertTrue(any(".xtb" in m for m in problems))

    def test_rename_and_copy_headers_are_rejected(self):
        for kind in ("rename", "copy"):
            body = f"diff --git a/f b/g\n{kind} from f\n{kind} to g\n" + diff("g", "a", "b")
            self.assertTrue(any("rename/copy" in m for m in self.lint(body)), kind)

    def test_patches_may_not_edit_files_helium_resources_overwrites(self):
        core = Path(self._tmp.name) / "core"
        (core / "resources").mkdir()
        (core / "resources" / "helium_resources.txt").write_text(
            "# c\nfavicons/a.png chrome/app/theme/a.png\n\nbad\n")
        self.rules = patches.load_substitution_rules(core)
        self.assertEqual(self.rules.resource_destinations, {"chrome/app/theme/a.png"})
        problems = self.lint(diff("chrome/app/theme/a.png", "x", "y"))
        self.assertEqual(len(problems), 1)
        self.assertIn("helium_resources.txt", problems[0])
        self.assertEqual(self.lint(diff("chrome/app/theme/b.png", "x", "y")), [])

    def test_an_added_line_that_looks_like_a_file_header_is_still_an_added_line(self):
        body = "--- a/net/listed.cc\n+++ b/net/listed.cc\n@@ -1 +1,2 @@\n x\n+++ www.google.com/y\n"
        self.assertEqual(patches.added_lines(body), [("net/listed.cc", "++ www.google.com/y")])
        self.assertEqual(len(self.lint(body)), 1)

    def test_added_lines_follow_hunks_and_files(self):
        body = ("--- a/f\n+++ b/f\n@@ -1,2 +1,3 @@\n c\n+one\n-gone\n\\ No newline at end of file\n"
                "+two\n--- a/g\n+++ b/g\n@@ -0,0 +1 @@\n+three\n")
        self.assertEqual(patches.added_lines(body), [("f", "one"), ("f", "two"), ("g", "three")])

    def test_without_rules_only_the_basic_checks_run(self):
        (self.dir / "a.patch").write_text(GOOD_HEADER + diff("c/s.grd", "x", "Chrome"))
        (self.dir / "series").write_text("a.patch\n")
        self.assertEqual(patches.lint_patch_dir(self.dir), [])

    def test_the_real_helium_lists_load(self):
        rules = patches.load_substitution_rules(Path(__file__).resolve().parents[1] / "helium-chromium")
        self.assertGreater(len(rules.domain_patterns), 5)
        self.assertGreater(len(rules.domain_files), 1000)


class ApplyTest(unittest.TestCase):
    """Applies real patches (GNU patch) to a tiny synthetic tree."""

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        root = Path(self._tmp.name)
        self.tree = root / "tree"
        self.tree.mkdir()
        (self.tree / "brand.txt").write_text("name=Chromium\nowner=x\n")
        (self.tree / "other.txt").write_text("keep\n")
        self.upstream = root / "upstream"
        self.aerie = root / "aerie"
        self.layers = [
            patches.Layer("helium", self.upstream, fuzz=True),
            patches.Layer("aerie", self.aerie, fuzz=False),
        ]
        self._write(self.upstream, "0001-helium.patch", "name=Chromium", "name=Helium", "brand.txt")
        self._write(self.aerie, "0001-aerie.patch", "name=Helium", "name=Aerie", "brand.txt")
        self._write(self.aerie, "0002-other.patch", "keep", "kept", "other.txt")

    def tearDown(self):
        self._tmp.cleanup()

    @staticmethod
    def _write(directory: Path, name: str, old: str, new: str, path: str):
        directory.mkdir(parents=True, exist_ok=True)
        (directory / name).write_text(
            f"Aerie-Purpose: t\nAerie-Upstream-Area: {path}\n\n"
            f"--- a/{path}\n+++ b/{path}\n@@ -1 +1 @@\n-{old}\n+{new}\n"
        )
        series = directory / "series"
        existing = series.read_text() if series.exists() else ""
        if name not in existing:
            series.write_text(existing + name + "\n")

    def statuses(self, results):
        return {r.patch: r.status for r in results}

    def test_full_series_applies_in_layer_order(self):
        results = patches.apply_layers(self.tree, self.layers)
        self.assertTrue(all(r.status == "ok" for r in results), results)
        self.assertEqual([r.patch for r in results],
                         ["helium/0001-helium.patch", "aerie/0001-aerie.patch", "aerie/0002-other.patch"])
        self.assertEqual((self.tree / "brand.txt").read_text(), "name=Aerie\nowner=x\n")

    def test_broken_patch_is_named_and_leaves_the_tree_untouched(self):
        self._write(self.aerie, "0001-aerie.patch", "name=Nope", "name=Aerie", "brand.txt")
        results = patches.apply_layers(self.tree, self.layers)
        st = self.statuses(results)
        self.assertEqual(st["aerie/0001-aerie.patch"], "failed")
        self.assertEqual((self.tree / "brand.txt").read_text(), "name=Helium\nowner=x\n")
        self.assertFalse(list(self.tree.glob("*.rej")))
        self.assertFalse(list(self.tree.glob("*.orig")))

    def test_independent_patch_after_a_failure_is_still_checked(self):
        self._write(self.aerie, "0001-aerie.patch", "name=Nope", "name=Aerie", "brand.txt")
        st = self.statuses(patches.apply_layers(self.tree, self.layers))
        self.assertEqual(st["aerie/0002-other.patch"], "ok")

    def test_dependent_patch_is_skipped_and_names_the_failure(self):
        self._write(self.aerie, "0001-aerie.patch", "name=Nope", "name=Aerie", "brand.txt")
        self._write(self.aerie, "0003-dependent.patch", "name=Aerie", "name=Zed", "brand.txt")
        results = patches.apply_layers(self.tree, self.layers)
        dependent = next(r for r in results if r.patch == "aerie/0003-dependent.patch")
        self.assertEqual(dependent.status, "skipped")
        self.assertIn("aerie/0001-aerie.patch", dependent.detail)

    def test_skip_spreads_to_patches_that_need_what_a_skipped_patch_would_have_made(self):
        self._write(self.aerie, "0001-aerie.patch", "name=Nope", "name=Aerie", "brand.txt")
        # Skipped: it also edits brand.txt, and it would have changed other.txt.
        (self.aerie / "0003-both.patch").write_text(
            "--- a/brand.txt\n+++ b/brand.txt\n@@ -1 +1 @@\n-name=Aerie\n+name=Zed\n"
            "--- a/other.txt\n+++ b/other.txt\n@@ -1 +1 @@\n-keep\n+held\n"
        )
        # Touches only other.txt, which the skipped patch was meant to change first.
        (self.aerie / "0004-chain.patch").write_text(
            "--- a/other.txt\n+++ b/other.txt\n@@ -1 +1 @@\n-held\n+last\n"
        )
        (self.aerie / "series").write_text("\n".join(
            ["0001-aerie.patch", "0003-both.patch", "0004-chain.patch"]) + "\n")
        st = self.statuses(patches.apply_layers(self.tree, self.layers))
        self.assertEqual(st["aerie/0003-both.patch"], "skipped")
        self.assertEqual(st["aerie/0004-chain.patch"], "skipped")

    def test_fuzz_is_off_for_aerie_and_on_for_helium(self):
        # The trailing context line does not match the tree. Fuzz may drop it,
        # strict mode may not. Same patch, only the layer setting differs.
        for layer, expect in ((self.layers[0], "ok"), (self.layers[1], "failed")):
            layer.directory.mkdir(parents=True, exist_ok=True)
            (layer.directory / "0009-ctx.patch").write_text(
                "Aerie-Purpose: t\nAerie-Upstream-Area: brand.txt\n\n"
                "--- a/brand.txt\n+++ b/brand.txt\n@@ -1,3 +1,3 @@\n"
                " name=Chromium\n-owner=x\n+owner=y\n WRONG-CONTEXT\n"
            )
            (layer.directory / "series").write_text("0009-ctx.patch\n")
            tree = Path(tempfile.mkdtemp(dir=self._tmp.name))
            (tree / "brand.txt").write_text("name=Chromium\nowner=x\nlast\n")
            results = patches.apply_layers(tree, [layer])
            self.assertEqual(results[0].status, expect, (layer.label, results))


class ReportTest(unittest.TestCase):
    def test_report_names_failures_and_sets_exit_status(self):
        results = [
            patches.PatchResult("helium/a.patch", "ok", ""),
            patches.PatchResult("aerie/b.patch", "failed", "Hunk #1 FAILED"),
            patches.PatchResult("aerie/c.patch", "skipped", "depends on failed aerie/b.patch"),
        ]
        text, ok = patches.format_report(results)
        self.assertFalse(ok)
        self.assertIn("FAILED  aerie/b.patch", text)
        self.assertIn("SKIPPED aerie/c.patch", text)
        self.assertIn("1 failed, 1 skipped, 1 applied", text)

    def test_all_ok_report(self):
        text, ok = patches.format_report([patches.PatchResult("a", "ok", "")])
        self.assertTrue(ok)
        self.assertIn("1 applied", text)


class DirectoryExtractTest(unittest.TestCase):
    """A wanted entry ending in / brings in the whole directory."""

    FILES = {"top/tools/grit/grit/__init__.py": "i", "top/tools/grit/grit/extern/FP.py": "fp",
             "top/tools/grit/other.py": "o", "top/a.txt": "a"}

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.root = Path(self._tmp.name)
        src = self.root / "src"
        for rel, text in self.FILES.items():
            path = src / rel
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text)
        self.archives = {}
        self.archives["tar"] = self.root / "a.tar"
        subprocess.run(["tar", "-cf", str(self.archives["tar"]), "-C", str(src), "top"], check=True)
        self.archives["tar.gz"] = self.root / "a.tar.gz"
        subprocess.run(["tar", "-czf", str(self.archives["tar.gz"]), "-C", str(src), "top"], check=True)
        if shutil.which("zstd"):
            self.archives["tar.zst"] = self.root / "a.tar.zst"
            subprocess.run(["zstd", "-q", "-o", str(self.archives["tar.zst"]), str(self.archives["tar"])], check=True)
        import zipfile
        self.archives["zip"] = self.root / "a.zip"
        with zipfile.ZipFile(self.archives["zip"], "w") as zf:
            for rel, text in self.FILES.items():
                zf.writestr(rel, text)

    def tearDown(self):
        self._tmp.cleanup()

    def test_every_archive_format_extracts_the_directory_and_only_it(self):
        for kind, archive in self.archives.items():
            with self.subTest(kind):
                dest = self.root / f"out-{kind}"
                found = patches.extract_wanted(archive, "top", ["tools/grit/grit/", "a.txt"], dest)
                self.assertEqual(found, {"tools/grit/grit/", "a.txt"})
                self.assertEqual((dest / "tools/grit/grit/extern/FP.py").read_text(), "fp")
                self.assertFalse((dest / "tools/grit/other.py").exists())

    def test_a_directory_the_archive_lacks_is_not_found(self):
        for kind, archive in self.archives.items():
            with self.subTest(kind):
                found = patches.extract_wanted(archive, "top", ["no/such/dir/"], self.root / f"o2-{kind}")
                self.assertEqual(found, set())


class SparseTreeTest(unittest.TestCase):
    def test_wanted_files_unions_series_across_layers(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            a, b = root / "a", root / "b"
            for d, rel in ((a, "x/one.cc"), (b, "y/two.cc")):
                d.mkdir()
                (d / "p.patch").write_text(diff(rel, "1", "2"))
                (d / "series").write_text("p.patch\n")
            layers = [patches.Layer("a", a, True), patches.Layer("b", b, False)]
            self.assertEqual(patches.wanted_files(layers), ["x/one.cc", "y/two.cc"])

    def test_extract_wanted_pulls_only_listed_members(self):
        import subprocess
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            src = root / "chromium-1" / "d"
            src.mkdir(parents=True)
            (src / "keep.txt").write_text("k")
            (src / "drop.txt").write_text("d")
            tarball = root / "c.tar"
            subprocess.run(["tar", "-cf", str(tarball), "-C", str(root), "chromium-1"], check=True)
            dest = root / "sparse"
            found = patches.extract_wanted(tarball, "chromium-1", ["d/keep.txt", "d/absent.txt"], dest)
            self.assertEqual(found, {"d/keep.txt"})
            self.assertTrue((dest / "d" / "keep.txt").exists())
            self.assertFalse((dest / "d" / "drop.txt").exists())

    @unittest.skipUnless(shutil.which("zstd"), "zstd not installed")
    def test_zstd_tarball_through_a_symlink_and_corrupt_input(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            src = root / "chromium-1"
            src.mkdir()
            (src / "keep.txt").write_text("k")
            plain = root / "c.tar"
            subprocess.run(["tar", "-cf", str(plain), "-C", str(root), "chromium-1"], check=True)
            subprocess.run(["zstd", "-q", str(plain), "-o", str(root / "real.tar.zst")], check=True)
            link = root / "link.tar.zst"
            link.symlink_to(root / "real.tar.zst")
            found = patches.extract_wanted(link, "chromium-1", ["keep.txt"], root / "out")
            self.assertEqual(found, {"keep.txt"})
            bad = root / "bad.tar.zst"
            bad.write_bytes(b"not zstd at all")
            with self.assertRaises(RuntimeError):
                patches.extract_wanted(bad, "chromium-1", ["keep.txt"], root / "out2")


class OffsetTest(unittest.TestCase):
    """A hunk that lands at a shifted line means the context drifted."""

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        root = Path(self._tmp.name)
        self.tree = root / "tree"
        self.tree.mkdir()
        # Two lines were added above the hunk since the patch was written.
        (self.tree / "f.txt").write_text("new1\nnew2\n" + "".join(f"line{i}\n" for i in range(1, 21)))
        self.dir = root / "p"
        self.dir.mkdir()
        (self.dir / "a.patch").write_text(
            "--- a/f.txt\n+++ b/f.txt\n@@ -9,3 +9,3 @@\n line9\n-line10\n+LINE10\n line11\n"
        )
        (self.dir / "series").write_text("a.patch\n")

    def tearDown(self):
        self._tmp.cleanup()

    def test_strict_layer_rejects_an_offset(self):
        layer = patches.Layer("aerie", self.dir, fuzz=False)
        results = patches.apply_layers(self.tree, [layer])
        self.assertEqual(results[0].status, "failed")
        self.assertIn("offset", results[0].detail)
        self.assertIn("line10", (self.tree / "f.txt").read_text())

    def test_patch_layer_rejects_an_offset_and_stages_nothing(self):
        layer = patches.Layer("aerie", self.dir, fuzz=False)
        ws = mock.Mock(stage_dir=self.tree.parent / "stage")
        ws.stage_dir.mkdir()
        ws.read = lambda rel: (self.tree / rel).read_bytes()
        ws.materialize = lambda rels: [(ws.stage_dir / r).write_bytes(ws.read(r)) for r in rels]
        with self.assertRaises(patches.PatchStateError) as ctx:
            patches.PatchLayer(layer).apply(ws)
        self.assertIn("offset", str(ctx.exception))
        ws.put.assert_not_called()

    def test_fuzzy_layer_still_accepts_an_offset(self):
        layer = patches.Layer("helium", self.dir, fuzz=True)
        self.assertEqual(patches.apply_layers(self.tree, [layer])[0].status, "ok")


class PatchLocaleTest(unittest.TestCase):
    def test_patch_runs_with_the_c_locale_so_offset_messages_are_english(self):
        from unittest import mock
        done = subprocess.CompletedProcess([], 0, stdout="", stderr="")
        with mock.patch.dict(os.environ, {"LC_ALL": "de_DE.UTF-8", "LANG": "de_DE.UTF-8"}), \
             mock.patch.object(patches.subprocess, "run", return_value=done) as run:
            patches._run_patch(Path("/t"), Path("/p.patch"), fuzz=False, dry_run=True)
        self.assertEqual(run.call_args.kwargs["env"]["LC_ALL"], "C")


class TruncatedArchiveTest(unittest.TestCase):
    def make(self, root: Path, count: int = 400) -> Path:
        src = root / "chromium-1"
        src.mkdir()
        for i in range(count):
            (src / f"f{i}.txt").write_text(os.urandom(2000).hex())
        plain = root / "c.tar"
        subprocess.run(["tar", "-cf", str(plain), "-C", str(root), "chromium-1"], check=True)
        return plain

    def test_truncated_gzip_tarball_is_an_error(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            self.make(root)
            gz = root / "c.tar.gz"
            subprocess.run(["tar", "-czf", str(gz), "-C", str(root), "chromium-1"], check=True)
            gz.write_bytes(gz.read_bytes()[: gz.stat().st_size // 2])
            with self.assertRaises(RuntimeError):
                patches.extract_wanted(gz, "chromium-1", ["f399.txt", "f0.txt"], root / "out")

    def test_truncated_zip_is_an_error(self):
        import zipfile
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            z = root / "c.zip"
            with zipfile.ZipFile(z, "w") as zf:
                for i in range(50):
                    zf.writestr(f"p/f{i}.txt", os.urandom(2000).hex())
            z.write_bytes(z.read_bytes()[: z.stat().st_size // 2])
            with self.assertRaises(RuntimeError):
                patches.extract_wanted(z, "p", ["f1.txt"], root / "out")

    @unittest.skipUnless(shutil.which("zstd"), "zstd not installed")
    def test_truncated_zstd_tarball_is_an_error(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            plain = self.make(root)
            subprocess.run(["zstd", "-q", str(plain), "-o", str(root / "c.tar.zst")], check=True)
            data = (root / "c.tar.zst").read_bytes()
            (root / "c.tar.zst").write_bytes(data[: len(data) // 2])
            with self.assertRaises(RuntimeError):
                patches.extract_wanted(root / "c.tar.zst", "chromium-1", ["f399.txt", "f0.txt"], root / "out")

    def test_intact_archive_with_absent_names_still_reports_them(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            plain = self.make(root, count=5)
            found = patches.extract_wanted(plain, "chromium-1", ["f1.txt", "nope.txt"], root / "out")
            self.assertEqual(found, {"f1.txt"})


if __name__ == "__main__":
    unittest.main()
