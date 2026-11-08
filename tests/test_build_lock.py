import os
import subprocess
import sys
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

    def test_the_lock_yields_the_marker_it_exports(self):
        with buildlock.build_tree_lock(self.root) as marker:
            self.assertEqual(marker, f"{self.root / buildlock.LOCK_NAME}:{os.getpid()}")
            self.assertEqual(os.environ[buildlock.ENV_HELD], marker)
        self.assertNotIn(buildlock.ENV_HELD, os.environ)

    def test_holder_itself_is_not_let_through(self):
        with buildlock.build_tree_lock(self.root):
            with self.assertRaises(buildlock.BuildTreeBusy):
                with buildlock.build_tree_lock(self.root, timeout=0):
                    pass

    def test_marker_for_another_lock_or_a_non_ancestor_does_not_bypass(self):
        mine = str(self.root / buildlock.LOCK_NAME)
        with buildlock.build_tree_lock(self.root):
            # pid 1 is alive but is not a parent of this process in a way we accept (init is excluded)
            for marker in ("/elsewhere/build.lock:" + str(os.getppid()), f"{mine}:999999999", f"{mine}:1"):
                with mock.patch.dict(os.environ, {buildlock.ENV_HELD: marker}):
                    with self.assertRaises(buildlock.BuildTreeBusy):
                        with buildlock.build_tree_lock(self.root, timeout=0):
                            pass

    def test_a_live_pid_that_is_not_an_ancestor_does_not_bypass(self):
        """A marker left in a long-lived shell, or a reused pid: alive, but not our parent."""
        mine = str(self.root / buildlock.LOCK_NAME)
        sibling = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(60)"])
        self.addCleanup(sibling.wait)
        self.addCleanup(sibling.kill)
        with buildlock.build_tree_lock(self.root):
            with mock.patch.dict(os.environ, {buildlock.ENV_HELD: f"{mine}:{sibling.pid}"}):
                with self.assertRaises(buildlock.BuildTreeBusy):
                    with buildlock.build_tree_lock(self.root, timeout=0):
                        pass

    def test_a_permission_error_is_not_a_held_lock(self):
        mine = self.root / buildlock.LOCK_NAME
        grandparent = buildlock._parent_of(os.getppid())
        if not grandparent or grandparent <= 1:
            self.skipTest("no grandparent process to name")
        env = {buildlock.ENV_HELD: f"{mine}:{grandparent}"}
        self.assertTrue(buildlock.held_by_ancestor(mine, env))
        with mock.patch("builtins.open", side_effect=PermissionError):  # /proc/<ppid>/stat unreadable
            self.assertFalse(buildlock.held_by_ancestor(mine, env))


_TOOLS = str(Path(__file__).resolve().parents[1] / "tools")
_TRY = """
import sys
sys.path.insert(0, sys.argv[1])
from pathlib import Path
from aerie_build import buildlock
try:
    with buildlock.build_tree_lock(Path(sys.argv[2]), timeout=0):
        pass
except buildlock.BuildTreeBusy:
    sys.exit(3)
"""
_HOLDER = """
import os, subprocess, sys
sys.path.insert(0, sys.argv[1])
from pathlib import Path
from aerie_build import buildlock
with buildlock.build_tree_lock(Path(sys.argv[2])) as marker:
    # What the CI does: hand the marker to a child process through its environment.
    child = subprocess.run([sys.executable, "-c", sys.argv[3], sys.argv[1], sys.argv[2]],
                           env={**os.environ, buildlock.ENV_HELD: marker})
    print(marker, child.returncode, flush=True)
    sys.stdin.readline()
"""


class RealSubprocessTest(unittest.TestCase):
    """No mocks: a real child of the lock holder, and a real unrelated process."""

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.root = Path(self._tmp.name) / "root"

    def test_a_child_of_the_holder_gets_through_and_an_unrelated_process_does_not(self):
        env = {k: v for k, v in os.environ.items() if k != buildlock.ENV_HELD}
        holder = subprocess.Popen([sys.executable, "-c", _HOLDER, _TOOLS, str(self.root), _TRY],
                                  stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, env=env)
        self.addCleanup(holder.wait)
        self.addCleanup(holder.kill)
        marker, child_rc = holder.stdout.readline().split()
        self.assertEqual(child_rc, "0")  # the holder's child took the lock under the marker and did not wait
        # An unrelated process: it is not a descendant of the holder, even when it carries the marker.
        for extra in ({}, {buildlock.ENV_HELD: marker}):
            rc = subprocess.run([sys.executable, "-c", _TRY, _TOOLS, str(self.root)],
                                env={**env, **extra}, capture_output=True).returncode
            self.assertEqual(rc, 3)


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

    def test_bench_refuses_while_another_build_holds_the_lock(self):
        from aerie_bench import cli as bench_cli

        with mock.patch.object(bench_cli, "run", return_value=0) as run:
            with buildlock.build_tree_lock(self.root):
                code = cli.main(["bench", "--build-root", str(self.root), "--lock-timeout", "0"])
        self.assertEqual(code, 1)
        run.assert_not_called()

    def test_bench_runs_under_the_lock_and_releases_it(self):
        from aerie_bench import cli as bench_cli

        held = []
        with mock.patch.object(bench_cli, "run", side_effect=lambda *a: held.append(os.environ.get(buildlock.ENV_HELD)) or 0):
            self.assertEqual(cli.main(["bench", "--build-root", str(self.root), "--lock-timeout", "0"]), 0)
        self.assertTrue(held[0])
        with buildlock.build_tree_lock(self.root, timeout=0):
            pass


class LockedCommandTest(unittest.TestCase):
    """./aerie locked -- CMD holds the lock across several commands, e.g. a train's build plus Bench."""

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.root = Path(self._tmp.name)

    def run_locked(self, *command, timeout="0"):
        return cli.main(["locked", "--build-root", str(self.root), "--lock-timeout", timeout, "--", *command])

    def test_runs_the_command_and_returns_its_exit_code(self):
        self.assertEqual(self.run_locked(sys.executable, "-c", "raise SystemExit(0)"), 0)
        self.assertEqual(self.run_locked(sys.executable, "-c", "raise SystemExit(7)"), 7)

    def test_the_command_holds_the_lock_and_its_children_pass_through_it(self):
        out = self.root / "rc"
        child = ("import subprocess, sys, pathlib;"
                 f"sys.path.insert(0, {_TOOLS!r});"
                 f"rc = subprocess.run([sys.executable, '-c', {_TRY!r}, {_TOOLS!r}, {str(self.root)!r}]).returncode;"
                 f"pathlib.Path({str(out)!r}).write_text(str(rc))")
        self.assertEqual(self.run_locked(sys.executable, "-c", child), 0)
        self.assertEqual(out.read_text(), "0")  # a build started by the command does not wait

    def test_other_builds_wait_while_it_runs_and_get_the_lock_after(self):
        seen = []

        def probe():
            try:
                with buildlock.build_tree_lock(self.root, timeout=0, sleep=lambda s: None):
                    seen.append("free")
            except buildlock.BuildTreeBusy:
                seen.append("held")

        real = subprocess.run
        with mock.patch.object(subprocess, "run", side_effect=lambda *a, **k: (probe(), real(*a, **k))[1]):
            self.assertEqual(self.run_locked(sys.executable, "-c", "pass"), 0)
        self.assertEqual(seen, ["held"])
        with buildlock.build_tree_lock(self.root, timeout=0):
            pass

    def test_refuses_while_another_build_holds_the_lock(self):
        with buildlock.build_tree_lock(self.root):
            self.assertEqual(self.run_locked(sys.executable, "-c", "pass"), 1)

    def test_needs_a_command(self):
        self.assertEqual(self.run_locked(), 2)
