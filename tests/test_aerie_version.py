"""Aerie's release number reaches the About page through chrome/VERSION."""

import tempfile
import unittest
from pathlib import Path

from aerie_build import aerie_version, layers, pipeline

REPO = Path(__file__).resolve().parent.parent
CHROME_VERSION = "MAJOR=157\nMINOR=0\nBUILD=8103\nPATCH=38\nHELIUM_MAJOR=0\nHELIUM_MINOR=21\nHELIUM_PATCH=1\nHELIUM_PLATFORM=1\n"


class ParseTest(unittest.TestCase):
    def test_three_numbers(self):
        self.assertEqual(aerie_version.parse_release("0.1.0\n"), (0, 1, 0))
        self.assertEqual(aerie_version.parse_release("12.34.56"), (12, 34, 56))

    def test_anything_else_is_an_error(self):
        for bad in ("", "1.2", "1.2.3.4", "v1.2.3", "1.2.x", "01.2.3", "-1.2.3", "1.2.3-rc1"):
            with self.assertRaises(aerie_version.VersionError, msg=bad):
                aerie_version.parse_release(bad)

    def test_the_committed_release_is_valid(self):
        aerie_version.parse_release((REPO / aerie_version.RELEASE_FILE).read_text())


class LayerTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        root = Path(self._tmp.name)
        self.repo = root / "repo"
        self.repo.mkdir()
        (self.repo / aerie_version.RELEASE_FILE).write_text("0.1.0\n")
        self.tree = root / "tree"
        (self.tree / "chrome").mkdir(parents=True)
        (self.tree / "chrome/VERSION").write_text(CHROME_VERSION)
        self.store, self.stage = root / "pristine", root / "stage"

    def apply(self):
        return layers.apply_layers(self.tree, self.store, [aerie_version.VersionLayer(self.repo)], self.stage)

    def version_file(self):
        return (self.tree / "chrome/VERSION").read_text()

    def test_adds_the_release_after_helium_s_lines(self):
        self.apply()
        self.assertEqual(self.version_file(), CHROME_VERSION + "AERIE_MAJOR=0\nAERIE_MINOR=1\nAERIE_PATCH=0\n")

    def test_second_run_writes_nothing_and_a_new_release_replaces_the_lines(self):
        self.apply()
        self.assertTrue(self.apply().idle)
        (self.repo / aerie_version.RELEASE_FILE).write_text("0.2.7\n")
        self.apply()
        text = self.version_file()
        self.assertIn("AERIE_MINOR=2\nAERIE_PATCH=7\n", text)
        self.assertEqual(text.count("AERIE_MAJOR"), 1)

    def test_a_file_without_a_final_newline_still_gets_clean_lines(self):
        (self.tree / "chrome/VERSION").write_text(CHROME_VERSION.rstrip("\n"))
        self.apply()
        self.assertIn("HELIUM_PLATFORM=1\nAERIE_MAJOR=0\n", self.version_file())

    def test_a_missing_tree_file_or_release_is_an_error(self):
        (self.tree / "chrome/VERSION").unlink()
        with self.assertRaisesRegex(aerie_version.VersionError, "chrome/VERSION"):
            self.apply()
        (self.tree / "chrome/VERSION").write_text(CHROME_VERSION)
        (self.repo / aerie_version.RELEASE_FILE).unlink()
        with self.assertRaisesRegex(aerie_version.VersionError, aerie_version.RELEASE_FILE):
            self.apply()


class WiringTest(unittest.TestCase):
    def test_the_layer_runs_before_the_substitution(self):
        ctx = type("C", (), {"repo": REPO, "tree": Path("/t"), "core": REPO / "helium-chromium"})()
        names = [layer.name for layer in pipeline.aerie_layers(ctx, lambda _m: None)]
        self.assertIn("aerie version", names)
        self.assertLess(names.index("aerie version"), names.index("helium-to-aerie substitution"))

    def test_the_patch_turns_the_about_version_into_the_aerie_release(self):
        patch = (REPO / "patches/branding/0006-show-the-aerie-release.patch").read_text()
        self.assertIn('+#define HELIUM_PRODUCT_VERSION "@AERIE_MAJOR@.@AERIE_MINOR@.@AERIE_PATCH@"', patch)


if __name__ == "__main__":
    unittest.main()
