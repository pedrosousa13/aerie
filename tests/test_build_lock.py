import os
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from aerie_build import buildlock, cli, pipeline


class BuildLockTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.root = Path(self._tmp.name) / "root"  # does not exist yet

    def test_lock_file_lives_in_the_build_root(self):
        with buildlock.build_tree_lock(self.root):
            self.assertTrue((self.root / buildlock.LOCK_NAME).is_file())

    def test_second_holder_is_refused_without_waiting(self):
        with buildlock.build_tree_lock(self.root):
            with self.assertRaises(buildlock.BuildTreeBusy):
                with buildlock.build_tree_lock(self.root, timeout=0):
                    pass

    def test_lock_is_released_on_exit_and_after_an_error(self):
        with self.assertRaises(RuntimeError):
            with buildlock.build_tree_lock(self.root):
                raise RuntimeError("boom")
        with buildlock.build_tree_lock(self.root, timeout=0):
            pass

    def test_waits_until_the_holder_lets_go(self):
        first = buildlock.build_tree_lock(self.root)
        first.__enter__()
        ticks = []

        def sleep(_):
            ticks.append(1)
            if len(ticks) == 2:
                first.__exit__(None, None, None)

        with buildlock.build_tree_lock(self.root, timeout=5, sleep=sleep, poll=0.01):
            pass
        self.assertEqual(len(ticks), 2)

    def test_timeout_gives_up(self):
        with buildlock.build_tree_lock(self.root):
            clock = iter([0.0, 1.0, 2.0, 3.0, 4.0])
            with self.assertRaises(buildlock.BuildTreeBusy):
                with buildlock.build_tree_lock(self.root, timeout=2, sleep=lambda s: None,
                                               monotonic=lambda: next(clock)):
                    pass

    def test_child_of_a_live_holder_does_not_deadlock(self):
        """A CI run holds the lock and starts ./aerie build, which takes it again."""
        marker = str(self.root / buildlock.LOCK_NAME)
        with buildlock.build_tree_lock(self.root):
            self.assertEqual(os.environ[buildlock.ENV_HELD], f"{marker}:{os.getpid()}")
            with mock.patch("os.getpid", return_value=os.getpid() + 1):  # pretend to be the child
                with buildlock.build_tree_lock(self.root, timeout=0):
                    pass
        self.assertNotIn(buildlock.ENV_HELD, os.environ)

    def test_holder_itself_is_not_let_through(self):
        with buildlock.build_tree_lock(self.root):
            with self.assertRaises(buildlock.BuildTreeBusy):
                with buildlock.build_tree_lock(self.root, timeout=0):
                    pass

    def test_marker_for_another_lock_or_a_dead_process_does_not_bypass(self):
        mine = str(self.root / buildlock.LOCK_NAME)
        with buildlock.build_tree_lock(self.root):
            for marker in ("/elsewhere/build.lock:1", f"{mine}:999999999"):
                with mock.patch.dict(os.environ, {buildlock.ENV_HELD: marker}):
                    with mock.patch("os.getpid", return_value=os.getpid() + 1):
                        with self.assertRaises(buildlock.BuildTreeBusy):
                            with buildlock.build_tree_lock(self.root, timeout=0):
                                pass


class BuildEntryPointTakesTheLockTest(unittest.TestCase):
    """./aerie build and prune go through the same lock the CI uses."""

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.root = Path(self._tmp.name)

    def test_build_refuses_while_another_build_holds_the_lock(self):
        with mock.patch.object(pipeline, "build") as build:
            with buildlock.build_tree_lock(self.root):
                code = cli.main(["build", "--build-root", str(self.root), "--lock-timeout", "0"])
        self.assertEqual(code, 1)
        build.assert_not_called()

    def test_build_runs_and_releases_when_free(self):
        with mock.patch.object(pipeline, "build", return_value=Path("/x/aerie")) as build:
            self.assertEqual(cli.main(["build", "--build-root", str(self.root), "--lock-timeout", "0"]), 0)
        build.assert_called_once()
        with buildlock.build_tree_lock(self.root, timeout=0):
            pass

    def test_prune_refuses_while_locked(self):
        with buildlock.build_tree_lock(self.root):
            code = cli.main(["prune", "--build-root", str(self.root), "--lock-timeout", "0", "-n"])
        self.assertEqual(code, 1)
