"""./aerie test-cpp: build and run Aerie's C++ unit tests; the test fonts are fetched only for it."""

import tempfile
import unittest
from pathlib import Path
from unittest import mock

from aerie_build import cli, pipeline, test_fonts


class BuildStepsTest(unittest.TestCase):
    def test_a_plain_build_has_no_test_fonts_step(self):
        titles = [title for title, _ in pipeline.build_steps(with_tests=False)]
        self.assertNotIn("test fonts", titles)
        self.assertIn("compile", titles)

    def test_a_test_build_fetches_the_fonts_after_the_tree_is_prepared(self):
        titles = [title for title, _ in pipeline.build_steps(with_tests=True)]
        self.assertGreater(titles.index("test fonts"), titles.index("prepare tree"))
        self.assertLess(titles.index("test fonts"), titles.index("configure"))

    def test_a_plain_build_never_fetches_the_fonts(self):
        ctx = mock.MagicMock()
        ctx.test_targets = ()
        ctx.tree.exists.return_value = False
        ctx.pristine_dir.exists.return_value = False
        with tempfile.TemporaryDirectory() as tmp:
            ctx.log_path = Path(tmp) / "log"
            noop = mock.Mock()
            patches = [mock.patch.object(pipeline, name, noop) for name in (
                "preflight", "install_ccache", "install_python_env", "fetch_sources",
                "prepare_tree", "configure", "compile_targets")]
            patches.append(mock.patch.object(pipeline, "isolated_env", return_value={}))
            fonts = mock.Mock()
            patches.append(mock.patch.object(test_fonts, "ensure", fonts))
            for p in patches:
                p.start()
                self.addCleanup(p.stop)
            pipeline.build(ctx)
        fonts.assert_not_called()


class TestCppTest(unittest.TestCase):
    def make_ctx(self, tmp):
        paths = mock.MagicMock()
        out = Path(tmp) / "out"
        out.mkdir()
        ctx = mock.MagicMock()
        ctx.out_dir = out
        ctx.paths = paths
        ctx.log_path = Path(tmp) / "log"
        for name, _args in pipeline.TEST_BINARIES:
            (out / name).write_text("")
        return ctx

    def run_tests(self, codes, targets=None):
        calls = []

        def fake_run(log, cmd, env, cwd=None, check=True):
            calls.append([str(c) for c in cmd])
            return codes.get(Path(str(cmd[0])).name, 0)

        with tempfile.TemporaryDirectory() as tmp:
            ctx = self.make_ctx(tmp)
            with mock.patch.object(pipeline, "build_tree") as build_tree, \
                    mock.patch.object(pipeline, "run", fake_run), \
                    mock.patch.object(pipeline, "isolated_env", return_value={}):
                result = pipeline.test_cpp(ctx, targets=targets)
        return result, calls, build_tree

    def test_all_pass_returns_zero_and_runs_every_target(self):
        result, calls, build_tree = self.run_tests({})
        self.assertEqual(result, 0)
        self.assertEqual(
            [Path(c[0]).name for c in calls],
            ["aerie_adblock_unittests", "aerie_adblock_content_unittests", "aerie_widevine_unittests",
             "aerie_adblock_common_unittests", "aerie_adblock_renderer_unittests"])
        build_tree.assert_called_once()

    def test_no_binary_needs_components_unittests(self):
        # Helium prunes test data that components_unittests bundles, so it cannot be built.
        self.assertNotIn("components_unittests", [name for name, _ in pipeline.TEST_BINARIES])

    def test_the_content_tests_run_without_an_x_server(self):
        _result, calls, _ = self.run_tests({})
        by_name = {Path(c[0]).name: c[1:] for c in calls}
        self.assertEqual(by_name["aerie_adblock_content_unittests"], ["--ozone-platform=headless"])
        self.assertEqual(by_name["aerie_adblock_unittests"], [])

    def test_one_failure_returns_non_zero_but_every_target_still_runs(self):
        result, calls, _ = self.run_tests({"aerie_adblock_unittests": 1})
        self.assertNotEqual(result, 0)
        self.assertEqual(len(calls), len(pipeline.TEST_BINARIES))

    def test_a_subset_can_be_selected(self):
        result, calls, _ = self.run_tests({}, targets=["aerie_widevine_unittests"])
        self.assertEqual(result, 0)
        self.assertEqual([Path(c[0]).name for c in calls], ["aerie_widevine_unittests"])

    def test_an_unknown_target_is_an_error(self):
        with self.assertRaises(pipeline.BuildError):
            self.run_tests({}, targets=["base_unittests"])


class CliTest(unittest.TestCase):
    def test_test_cpp_is_a_command_and_returns_the_result(self):
        with tempfile.TemporaryDirectory() as root, \
                mock.patch.object(pipeline, "test_cpp", return_value=3) as run:
            self.assertEqual(cli.main(["test-cpp", "--build-root", root, "--target", "aerie_widevine_unittests"]), 3)
        self.assertEqual(run.call_args.kwargs["targets"], ["aerie_widevine_unittests"])


if __name__ == "__main__":
    unittest.main()
