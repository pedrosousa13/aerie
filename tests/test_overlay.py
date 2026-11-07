"""The overlay: Aerie-owned files, staged into the source tree as a layer."""

import os
import tempfile
import unittest
from pathlib import Path

from aerie_build import layers, overlay, patches

OLD_NS = 1_000_000_000_000_000_000


class OverlayLayerTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        root = Path(self._tmp.name)
        self.root = root / "overlay"
        self.tree = root / "tree"
        self.store = root / "pristine"
        self.stage = root / "stage"
        self.tree.mkdir()
        self.logs = []

    def put(self, rel, text="x"):
        path = self.root / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)
        return path

    def upstream(self, rel, text):
        path = self.tree / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)
        os.utime(path, ns=(OLD_NS, OLD_NS))
        return path

    def apply(self, extra=()):
        return layers.apply_layers(
            self.tree, self.store, [overlay.OverlayLayer(self.root, self.logs.append), *extra], self.stage)

    def test_copies_files_into_the_tree(self):
        self.put("components/a/BUILD.gn", "one")
        self.put("third_party/b/README.chromium", "two")
        report = self.apply()
        self.assertEqual(len(report.changes), 2)
        self.assertEqual((self.tree / "components/a/BUILD.gn").read_text(), "one")
        self.assertEqual((self.tree / "third_party/b/README.chromium").read_text(), "two")

    def test_second_run_writes_nothing_and_updates_changed_files(self):
        self.put("a/f.txt", "one")
        self.apply()
        self.assertTrue(self.apply().idle)
        self.put("a/f.txt", "two")
        self.assertEqual(len(self.apply().changes), 1)
        self.assertEqual((self.tree / "a/f.txt").read_text(), "two")

    def test_refuses_to_overwrite_an_upstream_file(self):
        self.upstream("chrome/BUILD.gn", "upstream")
        self.put("chrome/BUILD.gn", "mine")
        with self.assertRaisesRegex(overlay.OverlayError, "already in the source tree"):
            self.apply()
        self.assertEqual((self.tree / "chrome/BUILD.gn").read_text(), "upstream")

    def test_refuses_a_file_an_earlier_layer_created_with_other_content(self):
        pdir = Path(self._tmp.name) / "patches"
        pdir.mkdir()
        (pdir / "series").write_text("n.patch\n")
        (pdir / "n.patch").write_text("--- /dev/null\n+++ b/a/new.txt\n@@ -0,0 +1 @@\n+patched\n")
        self.put("a/new.txt", "overlay")
        patched = patches.PatchLayer(patches.Layer("aerie", pdir, fuzz=False))
        with self.assertRaisesRegex(overlay.OverlayError, "already in the source tree"):
            layers.apply_layers(self.tree, self.store, [patched, overlay.OverlayLayer(self.root)], self.stage)

    def test_nothing_is_written_when_any_file_conflicts(self):
        self.upstream("b/f", "upstream")
        self.put("a/f", "mine")
        self.put("b/f", "mine")
        with self.assertRaises(overlay.OverlayError):
            self.apply()
        self.assertFalse((self.tree / "a/f").exists())

    def test_a_file_that_already_has_the_overlay_bytes_is_accepted(self):
        self.put("a/one", "same")
        self.upstream("a/one", "same")
        self.apply()
        self.assertEqual((self.tree / "a/one").read_text(), "same")

    def test_symlinked_parent_directory_is_refused(self):
        outside = Path(self._tmp.name) / "outside"
        outside.mkdir()
        os.symlink(outside, self.tree / "link")
        self.put("link/f", "x")
        with self.assertRaisesRegex(overlay.OverlayError, "outside the source tree|symlink"):
            self.apply()
        self.assertEqual(list(outside.iterdir()), [])

    def test_a_symlink_at_the_target_is_refused(self):
        outside = Path(self._tmp.name) / "outside.txt"
        os.symlink(outside, self.tree / "f")  # dangling
        self.put("f", "x")
        with self.assertRaisesRegex(overlay.OverlayError, "symlink"):
            self.apply()
        self.assertFalse(outside.exists())

    def test_unsafe_paths_are_refused(self):
        for rel in ("../x", "/abs", "a/../../x"):
            with self.assertRaises(overlay.OverlayError):
                overlay._safe_target(self.tree, rel)

    def test_files_that_left_the_overlay_are_removed(self):
        self.put("a/keep", "k")
        gone = self.put("a/gone", "g")
        self.apply()
        gone.unlink()
        report = self.apply()
        self.assertEqual([c.kind for c in report.changes], ["remove"])
        self.assertTrue((self.tree / "a/keep").exists())
        self.assertFalse((self.tree / "a/gone").exists())

    def test_an_overlay_file_edited_in_the_tree_is_put_back(self):
        self.put("a/f", "one")
        self.apply()
        (self.tree / "a/f").write_text("tampered")
        self.apply()
        self.assertEqual((self.tree / "a/f").read_text(), "one")

    def test_cargo_standalone_files_are_not_copied(self):
        self.put("c/rust/Cargo.toml")
        self.put("c/rust/Cargo.lock")
        self.put("c/rust/target/debug/big.o")
        self.put("c/rust/src/lib.rs")
        self.put("c/__pycache__/x.pyc")
        self.apply()
        found = sorted(p.relative_to(self.tree).as_posix() for p in self.tree.rglob("*") if p.is_file())
        self.assertEqual(found, ["c/rust/src/lib.rs"])

    def test_symlinks_in_the_overlay_are_refused(self):
        self.put("a/real", "x")
        os.symlink(self.root / "a/real", self.root / "a/link")
        with self.assertRaisesRegex(overlay.OverlayError, "not a regular file"):
            self.apply()

    def test_a_symlinked_directory_in_the_overlay_is_refused(self):
        outside = Path(self._tmp.name) / "elsewhere"
        outside.mkdir()
        (outside / "f").write_text("x")
        self.put("a/real", "x")
        os.symlink(outside, self.root / "a/dirlink")
        with self.assertRaisesRegex(overlay.OverlayError, "not a regular file|symlink"):
            overlay.collect(self.root)

    def test_digest_changes_with_content_and_paths(self):
        self.put("a/f", "one")
        first = overlay.digest(self.root)
        self.put("a/f", "two")
        self.assertNotEqual(first, overlay.digest(self.root))


if __name__ == "__main__":
    unittest.main()
