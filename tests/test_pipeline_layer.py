import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

from aerie_build import patches, pipeline, substitution


class PrepareTreeOrderTest(unittest.TestCase):
    """The Aerie layer sits after Helium's patches and after Helium's name and i18n steps."""

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        root = Path(self._tmp.name)
        self.repo = root / "repo"
        (self.repo / "patches").mkdir(parents=True)
        (self.repo / "patches" / "series").write_text("")
        self.tree = root / "tree"
        self.tree.mkdir()
        self.ctx = SimpleNamespace(
            repo=self.repo,
            tree=self.tree,
            core=self.repo / "helium-chromium",
            platform=self.repo / "helium-linux",
            python=Path("py"),
        )
        self.events: list[str] = []

    def tearDown(self):
        self._tmp.cleanup()

    def run_prepare(self, applied_now=1):
        def fake_run(_log, cmd, _env, cwd=None):
            self.events.append(Path(str(cmd[1])).name)

        def fake_apply(tree, layer, state, log):
            self.events.append("aerie-patches")
            return applied_now

        def fake_sub(tree, fingerprint, workers=None, state_path=None):
            self.events.append("aerie-substitution")
            return substitution.Stats()

        with mock.patch.object(pipeline, "run", fake_run), \
             mock.patch.object(patches, "apply_series", fake_apply), \
             mock.patch.object(substitution, "substitute_tree", fake_sub), \
             mock.patch.object(substitution, "helium_fingerprint", lambda *a: None):
            pipeline.prepare_tree(self.ctx, lambda _m: None, {})

    def test_stage_order(self):
        self.run_prepare()
        self.assertEqual(
            self.events,
            ["prune_binaries.py", "patches.py", "aerie-patches", "domain_substitution.py",
             "name_substitution.py", "i18n_apply.py", "aerie-substitution",
             "helium_version.py", "generate_resources.py", "replace_resources.py"],
        )

    def test_second_run_does_nothing(self):
        self.run_prepare()
        self.events.clear()
        self.run_prepare(applied_now=0)
        self.assertEqual(self.events, ["aerie-patches"])

    def test_new_patches_force_the_substitution_to_run_again(self):
        self.run_prepare()
        self.events.clear()
        self.run_prepare(applied_now=1)
        self.assertEqual(self.events, ["aerie-patches", "aerie-substitution"])

    def test_substitution_rules_changed_since_the_tree_was_substituted(self):
        self.run_prepare()
        (self.tree / ".aeriesub.stamp").write_text("some-other-digest\n")
        with self.assertRaises(pipeline.BuildError) as ctx:
            self.run_prepare(applied_now=0)
        self.assertIn("--clean", str(ctx.exception))


class PreflightLintTest(unittest.TestCase):
    def test_bad_aerie_patch_stops_the_build_early_and_is_named(self):
        with tempfile.TemporaryDirectory() as tmp:
            repo = Path(tmp)
            (repo / "patches").mkdir()
            (repo / "patches" / "series").write_text("x/missing.patch\n")
            with self.assertRaises(pipeline.BuildError) as ctx:
                pipeline.check_aerie_patches(repo)
            self.assertIn("x/missing.patch", str(ctx.exception))


if __name__ == "__main__":
    unittest.main()
