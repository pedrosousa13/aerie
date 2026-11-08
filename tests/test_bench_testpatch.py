"""The deliberately memory-heavy test patch and the tool that applies it on demand (issue #16)."""

import contextlib
import io
import re
import shutil
import tempfile
import unittest
from pathlib import Path

from aerie_bench import testpatch

REPO = Path(__file__).resolve().parent.parent
PATCH = REPO / "tools" / "aerie_bench" / "testdata" / "memory-hog.patch"
TARGET = "chrome/browser/chrome_browser_main_linux.cc"
BUILT_TREE = Path("/mnt/linux2/aerie-build/src/helium-0.21.1.1")


def synthetic_source() -> str:
    """A file the hunk applies to: filler up to its first line, then the hunk's old lines."""
    text = PATCH.read_text()
    hunk = text[text.index("@@ -"):]
    start = int(re.match(r"@@ -(\d+),", hunk).group(1))
    old = [line[1:] for line in hunk.splitlines()[1:] if line[:1] in " -"]
    return "".join(f"// filler {i}\n" for i in range(start - 1)) + "\n".join(old) + "\n"


class PatchFileTest(unittest.TestCase):
    def test_it_is_a_test_patch_outside_the_real_series(self):
        series = (REPO / "patches" / "series").read_text()
        self.assertNotIn("memory-hog", series)
        self.assertFalse(list((REPO / "patches").rglob("*memory-hog*")))
        self.assertIn("TEST ONLY", PATCH.read_text())
        self.assertEqual(testpatch.touched_files(PATCH.read_text()), [TARGET])

    def test_no_real_patch_touches_the_file_it_edits(self):
        # the claim in the patch header: no layer reads or rewrites this file
        for top in ("patches", "helium-chromium/patches", "helium-linux/patches"):
            for patch in (REPO / top).rglob("*.patch"):
                self.assertNotIn(TARGET, patch.read_text(errors="replace"), patch)

    def test_it_allocates_and_touches_enough_memory_to_trip_the_memory_threshold(self):
        text = PATCH.read_text()
        self.assertIn("768u << 20", text)
        self.assertIn("i += 4096", text)  # every page is written, so it is resident, not just reserved


class ToolTest(unittest.TestCase):
    def setUp(self):
        self.tree = Path(tempfile.mkdtemp())
        self.addCleanup(lambda: shutil.rmtree(self.tree, ignore_errors=True))
        (self.tree / TARGET).parent.mkdir(parents=True)
        self.original = synthetic_source()
        (self.tree / TARGET).write_text(self.original)

    def source(self):
        return (self.tree / TARGET).read_text()

    def test_apply_edits_the_tree_and_revert_restores_it_byte_for_byte(self):
        self.assertIn("applies cleanly", testpatch.check(self.tree))
        testpatch.apply(self.tree)
        self.assertIn("aerie_bench_memory_hog", self.source())
        self.assertIn("is applied", testpatch.check(self.tree))
        testpatch.revert(self.tree)
        self.assertEqual(self.source(), self.original)
        self.assertFalse(list(self.tree.glob(".aerie-testpatch-*")))

    def test_check_changes_nothing(self):
        testpatch.check(self.tree)
        self.assertEqual(self.source(), self.original)
        self.assertFalse(list(self.tree.glob(".aerie-testpatch-*")))

    def test_applying_twice_and_reverting_without_applying_are_refused(self):
        with self.assertRaises(testpatch.TestPatchError):
            testpatch.revert(self.tree)
        testpatch.apply(self.tree)
        with self.assertRaises(testpatch.TestPatchError):
            testpatch.apply(self.tree)
        self.assertEqual(self.source().count("aerie_bench_memory_hog"), 2)  # declaration and the loop

    def test_a_tree_the_patch_does_not_fit_is_left_alone(self):
        (self.tree / TARGET).write_text("int main() {}\n")
        with self.assertRaises(testpatch.TestPatchError):
            testpatch.apply(self.tree)
        self.assertEqual(self.source(), "int main() {}\n")
        self.assertFalse(list(self.tree.glob(".aerie-testpatch-*")))

    def test_a_directory_without_the_file_is_not_a_chromium_tree(self):
        (self.tree / TARGET).unlink()
        with self.assertRaises(testpatch.TestPatchError):
            testpatch.apply(self.tree)

    def test_the_aerie_checkout_is_refused(self):
        for path in (REPO, REPO / "tools", REPO.parent):
            with self.assertRaises(testpatch.TestPatchError):
                testpatch.apply(path)

    def test_unknown_patch_names_are_refused(self):
        for name in ("nope", "../memory-hog", ""):
            with self.assertRaises(testpatch.TestPatchError):
                testpatch.apply(self.tree, name)

    def test_the_command_line_reports_and_sets_the_exit_code(self):
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            self.assertEqual(testpatch.main(["apply", str(self.tree)]), 0)
            self.assertEqual(testpatch.main(["apply", str(self.tree)]), 1)
            self.assertEqual(testpatch.main(["revert", str(self.tree)]), 0)
        self.assertIn("applied memory-hog", out.getvalue())
        self.assertIn("already applied", err.getvalue())


@unittest.skipUnless((BUILT_TREE / TARGET).is_file(), "no built tree to read")
class RealFileTest(unittest.TestCase):
    def test_it_applies_to_a_copy_of_the_real_file_of_the_built_tree(self):
        tree = Path(tempfile.mkdtemp())
        self.addCleanup(lambda: shutil.rmtree(tree, ignore_errors=True))
        (tree / TARGET).parent.mkdir(parents=True)
        shutil.copy(BUILT_TREE / TARGET, tree / TARGET)  # a copy: the built tree is only read
        testpatch.apply(tree)
        self.assertIn("aerie_bench_memory_hog", (tree / TARGET).read_text())
        testpatch.revert(tree)
        self.assertEqual((tree / TARGET).read_bytes(), (BUILT_TREE / TARGET).read_bytes())


if __name__ == "__main__":
    unittest.main()
