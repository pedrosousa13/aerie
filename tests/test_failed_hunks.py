import tempfile
import unittest
from pathlib import Path

from aerie_build import patches

PATCH = """Aerie-Purpose: x
Aerie-Upstream-Area: y

--- a/one.txt
+++ b/one.txt
@@ -1,3 +1,3 @@
 a
-b
+B
 c
@@ -10,3 +10,3 @@
 j
-k
+K
 l
--- a/two.txt
+++ b/two.txt
@@ -1,2 +1,2 @@
 p
-q
+Q
"""


class FailedHunksTest(unittest.TestCase):
    def test_names_the_file_and_shows_the_hunk_that_failed(self):
        output = "checking file one.txt\nHunk #2 FAILED at 10.\n1 out of 2 hunks FAILED\nchecking file two.txt"
        text = patches.failed_hunks(PATCH, output)
        self.assertIn("one.txt", text)
        self.assertIn("hunk #2", text)
        self.assertIn("-k\n", text)
        self.assertIn("+K\n", text)
        self.assertNotIn("+B", text)
        self.assertNotIn("two.txt", text)

    def test_hunk_numbers_restart_in_each_file(self):
        output = "checking file one.txt\nchecking file two.txt\nHunk #1 FAILED at 1."
        text = patches.failed_hunks(PATCH, output)
        self.assertIn("two.txt", text)
        self.assertIn("+Q", text)
        self.assertNotIn("+K", text)

    def test_no_failed_hunk_gives_empty_text(self):
        self.assertEqual(patches.failed_hunks(PATCH, "checking file one.txt\nNo such file"), "")

    def test_long_hunk_is_cut(self):
        body = "".join(f"+line {i}\n" for i in range(200))
        patch = f"--- a/f\n+++ b/f\n@@ -1,0 +1,200 @@\n{body}"
        text = patches.failed_hunks(patch, "checking file f\nHunk #1 FAILED at 1.")
        self.assertIn("more lines", text)
        self.assertLess(len(text.splitlines()), 100)


class ApplyReportsHunkTest(unittest.TestCase):
    def test_failed_patch_detail_has_the_hunk(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            tree = root / "tree"
            tree.mkdir()
            (tree / "f.txt").write_text("x\ny\nz\n")
            pdir = root / "patches"
            pdir.mkdir()
            (pdir / "series").write_text("a.patch\n")
            (pdir / "a.patch").write_text("--- a/f.txt\n+++ b/f.txt\n@@ -1,3 +1,3 @@\n a\n-b\n+B\n c\n")
            results = patches.apply_layers(tree, [patches.Layer("aerie", pdir, fuzz=False)])
        self.assertEqual(results[0].status, "failed")
        self.assertIn("+B", results[0].detail)
        self.assertIn("f.txt", results[0].detail)


if __name__ == "__main__":
    unittest.main()
