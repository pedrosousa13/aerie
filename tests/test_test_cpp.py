"""./aerie test-cpp: build and run Aerie's C++ unit tests; the test fonts are fetched only for it."""

import tempfile
import unittest
from pathlib import Path
from unittest import mock

from aerie_build import cli, pipeline, test_fonts, test_webui_data


class BuildStepsTest(unittest.TestCase):
    def test_a_plain_build_has_no_test_fonts_step(self):
        titles = [title for title, _ in pipeline.build_steps(with_tests=False)]
        self.assertNotIn("test fonts", titles)
        self.assertIn("compile", titles)

    def test_a_test_build_fetches_the_fonts_after_the_tree_is_prepared(self):
        titles = [title for title, _ in pipeline.build_steps(with_tests=True)]
        self.assertGreater(titles.index("test fonts"), titles.index("prepare tree"))
        self.assertLess(titles.index("test fonts"), titles.index("configure"))

    def test_a_plain_build_has_no_webui_test_data_step(self):
        for with_tests in (False, True):
            titles = [title for title, _ in pipeline.build_steps(with_tests=with_tests)]
            self.assertNotIn("webui test data", titles)

    def test_a_ui_test_build_fetches_the_webui_test_data_after_the_tree_is_prepared(self):
        titles = [title for title, _ in pipeline.build_steps(with_tests=True, ui_tests=True)]
        self.assertGreater(titles.index("webui test data"), titles.index("prepare tree"))
        self.assertLess(titles.index("webui test data"), titles.index("configure"))

    def test_only_the_ui_binaries_need_the_webui_test_data(self):
        self.assertTrue(pipeline.needs_webui_test_data(("aerie_browser_tests",)))
        self.assertTrue(pipeline.needs_webui_test_data(("aerie_adblock_unittests", "aerie_interactive_ui_tests")))
        self.assertFalse(pipeline.needs_webui_test_data(("aerie_adblock_unittests",)))
        self.assertFalse(pipeline.needs_webui_test_data(()))

    def test_the_webui_step_passes_the_lock_pin_to_the_fetcher(self):
        ctx = mock.MagicMock()
        ctx.pins.test_data = {"webui": {"sha256": "x"}}
        ctx.pins.chromium_version = "1.2.3.4"
        with mock.patch.object(test_webui_data, "ensure") as ensure:
            pipeline.fetch_webui_test_data(ctx, mock.Mock(), {})
        ensure.assert_called_once_with(ctx.tree, ctx.paths.downloads, {"sha256": "x"}, "1.2.3.4", ctx.opener, mock.ANY)

    def test_a_webui_fetch_failure_stops_the_build_with_a_build_error(self):
        ctx = mock.MagicMock()
        with mock.patch.object(test_webui_data, "ensure", side_effect=test_webui_data.TestDataError("boom")):
            with self.assertRaisesRegex(pipeline.BuildError, "boom"):
                pipeline.fetch_webui_test_data(ctx, mock.Mock(), {})

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
        for name, _args in pipeline.ALL_TEST_BINARIES:
            (out / name).write_text("")
        return ctx

    def run_tests(self, codes, targets=None, gtest_filter=None, xvfb="/usr/bin/xvfb-run"):
        calls = []

        def fake_run(log, cmd, env, cwd=None, check=True):
            calls.append([str(c) for c in cmd])
            names = [Path(str(c)).name for c in cmd]
            return next((code for name, code in codes.items() if name in names), 0)

        with tempfile.TemporaryDirectory() as tmp:
            ctx = self.make_ctx(tmp)
            with mock.patch.object(pipeline, "build_tree") as build_tree, \
                    mock.patch.object(pipeline, "run", fake_run), \
                    mock.patch.object(pipeline.shutil, "which", return_value=xvfb), \
                    mock.patch.object(pipeline, "isolated_env", return_value={}):
                result = pipeline.test_cpp(ctx, targets=targets, gtest_filter=gtest_filter)
        return result, calls, build_tree

    def test_all_pass_returns_zero_and_runs_every_target(self):
        result, calls, build_tree = self.run_tests({})
        self.assertEqual(result, 0)
        self.assertEqual(
            [Path(c[0]).name for c in calls],
            ["aerie_adblock_unittests", "aerie_adblock_content_unittests", "aerie_widevine_unittests",
             "aerie_adblock_common_unittests", "aerie_adblock_renderer_unittests", "aerie_fingerprint_unittests",
             "aerie_gpu_info_unittests", "aerie_screen_unittests", "aerie_text_metrics_unittests", "aerie_webgl_readback_unittests", "aerie_fonts_unittests",
             "aerie_design_tokens_unittests", "aerie_tab_model_unittests"])
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

    def test_the_ui_test_binaries_are_not_in_the_default_run(self):
        # They link Chromium's whole test support; a plain `./aerie test-cpp` stays a unit test run.
        _result, calls, build_tree = self.run_tests({})
        names = [Path(c[0]).name for c in calls]
        self.assertNotIn("aerie_browser_tests", names)
        self.assertNotIn("aerie_interactive_ui_tests", names)

    def test_the_ui_test_binaries_can_be_selected(self):
        result, calls, build_tree = self.run_tests({}, targets=["aerie_browser_tests", "aerie_interactive_ui_tests"])
        self.assertEqual(result, 0)
        self.assertEqual(build_tree.call_count, 1)
        self.assertEqual(len(calls), 2)

    def test_the_browser_tests_run_headless(self):
        _result, calls, _ = self.run_tests({}, targets=["aerie_browser_tests"])
        self.assertEqual(calls[0][1:], ["--ozone-platform=headless"])

    def test_the_interactive_tests_run_under_xvfb(self):
        _result, calls, _ = self.run_tests({}, targets=["aerie_interactive_ui_tests"])
        self.assertEqual(Path(calls[0][0]).name, "xvfb-run")
        self.assertIn("-a", calls[0])
        self.assertEqual(Path(calls[0][-1]).name, "aerie_interactive_ui_tests")

    def test_the_interactive_tests_fail_with_a_name_when_xvfb_is_missing(self):
        result, calls, _ = self.run_tests({}, targets=["aerie_interactive_ui_tests"], xvfb=None)
        self.assertNotEqual(result, 0)
        self.assertEqual(calls, [])

    def test_a_failing_ui_test_makes_the_command_fail(self):
        result, _calls, _ = self.run_tests({"aerie_browser_tests": 1}, targets=["aerie_browser_tests", "aerie_interactive_ui_tests"])
        self.assertNotEqual(result, 0)

    def test_a_gtest_filter_goes_to_every_selected_binary(self):
        _result, calls, _ = self.run_tests({}, targets=["aerie_browser_tests", "aerie_interactive_ui_tests"], gtest_filter="Aerie*")
        self.assertEqual(len(calls), 2)
        for call in calls:
            self.assertIn("--gtest_filter=Aerie*", call)

    def test_no_gtest_filter_adds_no_argument(self):
        _result, calls, _ = self.run_tests({}, targets=["aerie_browser_tests"])
        self.assertFalse([a for a in calls[0] if a.startswith("--gtest_filter")])

    def test_an_unknown_target_is_an_error(self):
        with self.assertRaises(pipeline.BuildError):
            self.run_tests({}, targets=["base_unittests"])


class CliTest(unittest.TestCase):
    def test_test_cpp_is_a_command_and_returns_the_result(self):
        with tempfile.TemporaryDirectory() as root, \
                mock.patch.object(pipeline, "test_cpp", return_value=3) as run:
            self.assertEqual(cli.main(["test-cpp", "--build-root", root, "--target", "aerie_widevine_unittests"]), 3)
        self.assertEqual(run.call_args.kwargs["targets"], ["aerie_widevine_unittests"])

    def test_the_ui_targets_and_the_filter_reach_the_pipeline(self):
        with tempfile.TemporaryDirectory() as root, \
                mock.patch.object(pipeline, "test_cpp", return_value=0) as run:
            cli.main(["test-cpp", "--build-root", root, "--target", "aerie_browser_tests",
                      "--target", "aerie_interactive_ui_tests", "--gtest-filter", "Aerie*"])
        self.assertEqual(run.call_args.kwargs["targets"], ["aerie_browser_tests", "aerie_interactive_ui_tests"])
        self.assertEqual(run.call_args.kwargs["gtest_filter"], "Aerie*")


if __name__ == "__main__":
    unittest.main()
