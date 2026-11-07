import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

from aerie_build import config, layers, pipeline


class PrepareTreeOrderTest(unittest.TestCase):
    """Helium's steps run once, in order, and the Aerie layers run last, on every build."""

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

    def run_prepare(self):
        def fake_run(_log, cmd, _env, cwd=None):
            self.events.append(Path(str(cmd[1])).name)

        def fake_layers(ctx, log, dry_run=False):
            self.events.append("aerie-layers")
            return layers.Report()

        with mock.patch.object(pipeline, "run", fake_run), \
             mock.patch.object(pipeline, "apply_aerie_layers", fake_layers):
            pipeline.prepare_tree(self.ctx, lambda _m: None, {})

    def test_stage_order(self):
        self.run_prepare()
        self.assertEqual(
            self.events,
            ["prune_binaries.py", "patches.py", "domain_substitution.py", "name_substitution.py",
             "i18n_apply.py", "helium_version.py", "generate_resources.py", "replace_resources.py",
             "aerie-layers"],
        )

    def test_second_run_only_reapplies_the_aerie_layers(self):
        self.run_prepare()
        self.events.clear()
        self.run_prepare()
        self.assertEqual(self.events, ["aerie-layers"])


class LegacyTreeTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.root = Path(self._tmp.name)
        self.tree = self.root / "tree"
        self.tree.mkdir()
        self.ctx = SimpleNamespace(repo=self.root / "repo", tree=self.tree, pristine_dir=self.root / "p",
                                   paths=SimpleNamespace(tmp=self.root / "tmp"))

    def tearDown(self):
        self._tmp.cleanup()

    def test_a_tree_the_old_pipeline_changed_in_place_is_refused(self):
        for name in pipeline.LEGACY_STATE:
            (self.tree / name).write_text("{}")
            with mock.patch.object(layers, "apply_layers") as apply, self.assertRaises(pipeline.BuildError) as err:
                pipeline.apply_aerie_layers(self.ctx, lambda _m: None)
            self.assertIn("--clean", str(err.exception))
            self.assertIn(name, str(err.exception))
            apply.assert_not_called()
            (self.tree / name).unlink()

    def test_prepare_tree_refuses_it_before_running_any_step(self):
        (self.tree / ".aerie-patches.json").write_text("[]")
        self.ctx.core = self.ctx.platform = self.root
        self.ctx.python = Path("py")
        with mock.patch.object(pipeline, "run") as run, self.assertRaises(pipeline.BuildError):
            pipeline.prepare_tree(self.ctx, lambda _m: None, {})
        run.assert_not_called()


class HeliumInputsTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.root = Path(self._tmp.name)
        self.repo = self.root / "repo"
        for rel, text in {"helium-chromium/patches/series": "a.patch\n", "helium-chromium/patches/a.patch": "x",
                          "helium-linux/patches/series": "", "helium-chromium/domain_regex.list": "r",
                          "patches/series": "", "patches/z.patch": "aerie"}.items():
            path = self.repo / rel
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text)
        self.tree = self.root / "tree"
        self.tree.mkdir()
        self.ctx = SimpleNamespace(repo=self.repo, tree=self.tree)

    def tearDown(self):
        self._tmp.cleanup()

    def check(self, **kw):
        pipeline.check_helium_inputs(self.ctx, lambda _m: None, **kw)

    def test_first_run_records_the_digest_and_unchanged_inputs_pass(self):
        self.check()
        self.assertTrue((self.tree / pipeline.HELIUM_INPUTS_STAMP).exists())
        self.check()

    def test_dry_run_does_not_write_the_stamp(self):
        self.check(write=False)
        self.assertFalse((self.tree / pipeline.HELIUM_INPUTS_STAMP).exists())

    def test_changing_a_helium_patch_needs_a_clean_build(self):
        self.check()
        (self.repo / "helium-chromium/patches/a.patch").write_text("changed")
        with self.assertRaises(pipeline.BuildError) as err:
            self.check()
        self.assertIn("--clean", str(err.exception))

    def test_changing_a_domain_list_needs_a_clean_build(self):
        self.check()
        (self.repo / "helium-chromium/domain_regex.list").write_text("other")
        with self.assertRaises(pipeline.BuildError):
            self.check()

    def test_drop_and_adapt_lists_count_wherever_they_live(self):
        for name in ("helium-drop.txt", "helium-adapt.txt"):
            with self.subTest(name):
                self.tearDown()
                self.setUp()
                self.check()
                path = self.repo / "tools" / "x" / name
                path.parent.mkdir(parents=True)
                path.write_text("one\n")
                with self.assertRaises(pipeline.BuildError):
                    self.check()
                (self.tree / pipeline.HELIUM_INPUTS_STAMP).unlink()
                self.check()
                path.write_text("two\n")
                with self.assertRaises(pipeline.BuildError):
                    self.check()

    def test_re_cutting_an_adapted_helium_patch_needs_a_clean_build(self):
        adapted = self.repo / "patches" / "helium-adapted"
        adapted.mkdir(parents=True)
        (adapted / "x.patch").write_text("one")
        self.check()
        (adapted / "x.patch").write_text("two")
        with self.assertRaises(pipeline.BuildError):
            self.check()
        (self.tree / pipeline.HELIUM_INPUTS_STAMP).unlink()
        self.check()
        (adapted / "new.patch").write_text("added")
        with self.assertRaises(pipeline.BuildError):
            self.check()

    def test_aerie_patches_do_not_count(self):
        self.check()
        (self.repo / "patches/z.patch").write_text("edited")
        self.check()


class AerieLayersWiringTest(unittest.TestCase):
    def test_layer_order_is_patches_then_substitution(self):
        ctx = SimpleNamespace(repo=Path(__file__).resolve().parents[1], core=Path("c"), tree=Path("t"))
        names = [layer.name for layer in pipeline.aerie_layers(ctx, lambda _m: None)]
        self.assertEqual(names, ["aerie patches", "helium-to-aerie substitution"])


class CleanTest(unittest.TestCase):
    def test_clean_removes_the_tree_and_the_pristine_snapshot(self):
        with tempfile.TemporaryDirectory() as tmp:
            paths = config.BuildPaths(Path(tmp))
            tree, pristine = paths.tree("helium-1"), paths.pristine / "helium-1"
            for d in (tree, pristine):
                d.mkdir(parents=True)
                (d / "f").write_text("x")
            ctx = SimpleNamespace(tree=tree, pristine_dir=pristine, paths=paths,
                                  log_path=paths.logs / "b.log", venv=paths.toolchain / "venv",
                                  binary=paths.root / "bin")
            steps = {n: mock.DEFAULT for n in ("install_ccache", "install_python_env", "fetch_sources",
                                                "prepare_tree", "configure", "compile_targets")}
            with mock.patch.multiple(pipeline, preflight=mock.DEFAULT, **steps):
                pipeline.build(ctx, clean=True)
            self.assertFalse(tree.exists())
            self.assertFalse(pristine.exists())

    def test_clean_and_fresh_unpack_take_the_store_lock(self):
        with tempfile.TemporaryDirectory() as tmp:
            paths = config.BuildPaths(Path(tmp))
            pristine = paths.pristine / "helium-1"
            pristine.mkdir(parents=True)
            ctx = SimpleNamespace(tree=paths.tree("helium-1"), pristine_dir=pristine)
            with mock.patch.object(layers, "locked", wraps=layers.locked) as locked:
                pipeline._fresh_tree(ctx, lambda _m: None)
            locked.assert_called_once_with(layers.lock_path(pristine))

    def test_unpacking_a_fresh_tree_discards_a_stale_snapshot(self):
        with tempfile.TemporaryDirectory() as tmp:
            paths = config.BuildPaths(Path(tmp))
            pristine = paths.pristine / "helium-1"
            pristine.mkdir(parents=True)
            ctx = SimpleNamespace(tree=paths.tree("helium-1"), pristine_dir=pristine)
            pipeline._fresh_tree(ctx, lambda _m: None)
            self.assertFalse(pristine.exists())
            self.assertTrue(ctx.tree.is_dir())


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
