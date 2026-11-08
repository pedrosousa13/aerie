import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from aerie_build import buildlock
from aerie_ci import gate

from .ci_fakes import ME, SHA_A, SHA_B, FakeGh, FakeShell, pr


class GateTestBase(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.root = Path(self._tmp.name) / "build"
        self.ci_root = Path(self._tmp.name) / "build-ci"  # outside the build root, see docs/ci.md
        self.repo = Path(self._tmp.name) / "repo"
        self.shell = FakeShell()
        self.gh = FakeGh()

    def cfg(self, **kw):
        kw.setdefault("environ", {"PATH": "/usr/bin", "GH_TOKEN": "ghp_" + "Z" * 36, "HOME": "/home/x"})
        kw.setdefault("lock_timeout", 0)
        kw.setdefault("python", "python3")
        return gate.CiConfig(repo=self.repo, build_root=self.root, shell=self.shell, gh=self.gh, **kw)


class AuthorFilterTest(GateTestBase):
    def test_pr_from_another_author_is_never_fetched_built_or_reported(self):
        outcome = gate.gate_pr(self.cfg(), pr(author="mallory"))
        self.assertEqual(outcome, gate.SKIPPED_AUTHOR)
        self.assertEqual(self.shell.calls, [])  # no git fetch, no worktree, nothing run
        self.assertEqual(self.gh.statuses, [])

    def test_author_match_is_exact_and_case_sensitive_on_the_login(self):
        for login in ("", "pedrosousa13-evil", "PedroSousa13x", "bot"):
            self.assertEqual(gate.gate_pr(self.cfg(), pr(author=login)), gate.SKIPPED_AUTHOR)
        self.assertEqual(self.shell.calls, [])

    def test_poll_only_gates_the_maintainers_prs(self):
        self.gh.prs = [pr(1, SHA_A, "mallory"), pr(2, SHA_B, ME)]
        self.shell.fetched_sha = SHA_B
        results = gate.poll(self.cfg())
        self.assertEqual([(p.number, o) for p, o in results], [(1, gate.SKIPPED_AUTHOR), (2, gate.SUCCESS)])
        fetched = [c for c in self.shell.joined() if "fetch" in c]
        self.assertEqual(len(fetched), 1)
        self.assertIn("pull/2/head", fetched[0])
        self.assertTrue(all(h == SHA_B for h, _, _ in self.gh.statuses))

    def test_no_pr_code_runs_before_the_author_check(self):
        """The check is the first thing gate_pr does: nothing at all runs for a stranger."""
        gate.gate_pr(self.cfg(), pr(author="mallory"))
        self.assertFalse(self.shell.ran("worktree"))
        self.assertFalse((self.ci_root).exists())


class DedupeTest(GateTestBase):
    def test_a_gated_sha_is_not_gated_again(self):
        cfg = self.cfg()
        self.assertEqual(gate.gate_pr(cfg, pr()), gate.SUCCESS)
        calls = len(self.shell.calls)
        statuses = len(self.gh.statuses)
        self.assertEqual(gate.gate_pr(cfg, pr()), gate.SKIPPED_GATED)
        self.assertEqual(len(self.shell.calls), calls)
        self.assertEqual(len(self.gh.statuses), statuses)

    def test_dedupe_survives_a_restart_and_covers_failures(self):
        self.shell.fail = {"aerie build": 1}
        self.assertEqual(gate.gate_pr(self.cfg(), pr()), gate.FAILURE)
        self.shell.calls.clear()
        self.assertEqual(gate.gate_pr(self.cfg(), pr()), gate.SKIPPED_GATED)  # new config, same state file
        self.assertEqual(self.shell.calls, [])

    def test_a_new_head_sha_on_the_same_pr_is_gated(self):
        cfg = self.cfg()
        gate.gate_pr(cfg, pr(1, SHA_A))
        self.shell.fetched_sha = SHA_B
        self.assertEqual(gate.gate_pr(cfg, pr(1, SHA_B)), gate.SUCCESS)

    def test_poll_twice_gates_once(self):
        self.gh.prs = [pr()]
        cfg = self.cfg()
        gate.poll(cfg)
        n = len(self.gh.statuses)
        results = gate.poll(cfg)
        self.assertEqual([o for _, o in results], [gate.SKIPPED_GATED])
        self.assertEqual(len(self.gh.statuses), n)


class StatusTest(GateTestBase):
    def test_pending_then_success_with_summary_and_log_path(self):
        gate.gate_pr(self.cfg(), pr())
        states = self.gh.states(SHA_A)
        self.assertEqual(states[0], "pending")
        self.assertEqual(states[-1], "success")
        self.assertNotIn("success", states[:-1])
        final = self.gh.statuses[-1][2]
        log = self.ci_root / "logs" / SHA_A[:12] / "gate.log"
        self.assertIn(str(log), final)
        self.assertIn("passed", final)
        self.assertTrue(log.is_file())

    def test_a_failing_step_gives_failure_naming_the_step_and_stops_the_run(self):
        self.shell.fail = {"build --build-root": 3}
        outcome = gate.gate_pr(self.cfg(), pr())
        self.assertEqual(outcome, gate.FAILURE)
        self.assertEqual(self.gh.states()[-1], "failure")
        self.assertIn("build", self.gh.statuses[-1][2])
        self.assertFalse(self.shell.ran("unittest"))
        self.assertFalse(self.shell.ran("--check smoke"))

    def test_running_step_is_visible_in_the_pending_status(self):
        gate.gate_pr(self.cfg(), pr())
        pending = [d for _, s, d in self.gh.statuses if s == "pending"]
        self.assertTrue(any("build" in d for d in pending))

    def test_harness_exception_posts_error_and_is_not_recorded(self):
        class Boom(FakeShell):
            def capture(self, argv, **kw):
                if "fetch" in argv or "rev-parse" in argv:
                    raise RuntimeError("git exploded")
                return super().capture(argv, **kw)

        self.shell = Boom()
        self.assertEqual(gate.gate_pr(self.cfg(), pr()), gate.ERROR)
        self.assertEqual(self.gh.states()[-1], "error")
        # A harness error says nothing about the PR, so the next poll gates this head again.
        self.shell = FakeShell()
        self.assertEqual(gate.gate_pr(self.cfg(), pr()), gate.SUCCESS)

    def test_a_worktree_left_by_a_killed_gate_does_not_block_the_sha(self):
        wt = str(self.ci_root / "worktrees" / SHA_A[:12])

        class Stale(FakeShell):
            stale = True

            def capture(self, argv, **kw):
                argv = [str(a) for a in argv]
                if "remove" in argv and wt in argv:
                    self.stale = False
                if "add" in argv and wt in argv and self.stale:
                    self.calls.append(argv)
                    raise RuntimeError(f"fatal: '{wt}' already exists")
                return super().capture(argv, **kw)

        self.shell = Stale()
        self.assertEqual(gate.gate_pr(self.cfg(), pr()), gate.SUCCESS)
        j = self.shell.joined()
        add = next(i for i, c in enumerate(j) if "worktree add" in c)
        self.assertTrue(any("worktree remove --force" in c for c in j[:add]))
        self.assertTrue(any("worktree prune" in c for c in j[:add]))

    def test_summary_json_is_written_next_to_the_log(self):
        gate.gate_pr(self.cfg(), pr())
        data = json.loads((self.ci_root / "logs" / SHA_A[:12] / "summary.json").read_text())
        self.assertEqual(data["sha"], SHA_A)
        self.assertEqual(data["state"], "success")
        self.assertEqual([s["name"] for s in data["steps"]],
                         ["patch check", "build", "unit tests", "smoke launch", "bench"])


class StepsTest(GateTestBase):
    def test_steps_run_in_order_in_their_own_worktree(self):
        gate.gate_pr(self.cfg(), pr(5))
        j = self.shell.joined()
        order = [next(i for i, c in enumerate(j) if needle in c) for needle in
                 ("pull/5/head", "worktree add", "patches check", "aerie build", "unittest", "--check smoke", "bench")]
        # 'bench' also matches the smoke line; the last bench call is the full one
        self.assertEqual(order[:6], sorted(order[:6]))
        wt = str(self.ci_root / "worktrees" / SHA_A[:12])
        self.assertIn(wt, [c for c in j if "worktree add" in c][0])
        self.assertTrue(self.shell.ran(f"worktree remove --force {wt}"))

    def test_worktree_is_removed_after_a_failure_too(self):
        self.shell.fail = {"patches check": 1}
        gate.gate_pr(self.cfg(), pr())
        self.assertTrue(self.shell.ran("worktree remove --force"))

    def test_fetched_sha_must_match_the_listed_head(self):
        self.shell.fetched_sha = SHA_B  # the PR moved between the list and the fetch
        self.assertEqual(gate.gate_pr(self.cfg(), pr(1, SHA_A)), gate.STALE)
        self.assertFalse(self.shell.ran("worktree add"))
        self.shell.fetched_sha = SHA_A
        self.assertEqual(gate.gate_pr(self.cfg(), pr(1, SHA_A)), gate.SUCCESS)

    def test_bench_runs_every_check_except_smoke_and_the_slow_zero_traffic(self):
        gate.gate_pr(self.cfg(), pr())
        bench = [c for c in self.shell.calls if "bench" in c and "--check" in c and "smoke" not in c]
        self.assertEqual(len(bench), 1)
        checks = [bench[0][i + 1] for i, a in enumerate(bench[0]) if a == "--check"]
        self.assertEqual(checks, ["ad_blocking", "branding"])

    def test_steps_use_the_existing_entry_points_with_the_shared_build_root(self):
        gate.gate_pr(self.cfg(), pr())
        wt = self.ci_root / "worktrees" / SHA_A[:12]
        for call in self.shell.calls:
            if "build" in call or "bench" in call:
                self.assertIn(str(wt / "aerie"), call)
                self.assertIn(str(self.root), call)

    def test_the_check_listing_uses_the_configured_interpreter(self):
        gate.gate_pr(self.cfg(python="/opt/py/bin/python"), pr())
        listing = next(c for c in self.shell.calls if "-c" in c)
        self.assertEqual(listing[0], "/opt/py/bin/python")

    def test_steps_do_not_get_tokens(self):
        gate.gate_pr(self.cfg(), pr())
        for env in self.shell.envs:
            self.assertNotIn("GH_TOKEN", env)
        self.assertTrue(any(e.get("PATH") == "/usr/bin" for e in self.shell.envs))


class LockTest(GateTestBase):
    def test_gate_waits_on_the_same_lock_as_the_build_entry_point(self):
        with buildlock.build_tree_lock(self.root):  # a build train is running
            outcome = gate.gate_pr(self.cfg(), pr())
        self.assertEqual(outcome, gate.BUSY)
        self.assertFalse(self.shell.ran("worktree add"))
        self.assertFalse(self.shell.ran("aerie build"))

    def test_busy_is_reported_and_retried_later(self):
        with buildlock.build_tree_lock(self.root):
            gate.gate_pr(self.cfg(), pr())
        self.assertEqual(self.gh.states()[-1], "error")
        self.assertIn("busy", self.gh.statuses[-1][2])
        self.assertEqual(gate.gate_pr(self.cfg(), pr()), gate.SUCCESS)  # not recorded as gated

    def test_every_step_gets_the_lock_marker_so_its_build_does_not_wait_for_us(self):
        gate.gate_pr(self.cfg(), pr())
        marker = f"{self.root / buildlock.LOCK_NAME}:{os.getpid()}"
        self.assertTrue(self.shell.envs)
        for env in self.shell.envs:
            self.assertEqual(env.get(buildlock.ENV_HELD), marker)
        self.assertNotIn(buildlock.ENV_HELD, os.environ)

    def test_lock_is_held_while_steps_run_and_released_after(self):
        seen = []
        shell = self.shell
        real_run = shell.run

        def run(argv, **kw):
            try:
                with buildlock.build_tree_lock(self.root, timeout=0):
                    seen.append("free")
            except buildlock.BuildTreeBusy:
                seen.append("held")
            return real_run(argv, **kw)

        shell.run = run
        gate.gate_pr(self.cfg(), pr())
        self.assertEqual(set(seen), {"held"})
        with buildlock.build_tree_lock(self.root, timeout=0):
            pass


class SecretsTest(GateTestBase):
    def test_no_token_reaches_the_log_or_any_status(self):
        token = "ghp_" + "Z" * 36

        class Leaky(FakeShell):
            def run(self, argv, *, cwd=None, env=None, log=None):
                if log is not None:
                    log.write(f"debug {token}\n")
                return super().run(argv, cwd=cwd, env=env, log=log)

        self.shell = Leaky()
        self.shell.fail = {"unittest": 1}
        gate.gate_pr(self.cfg(), pr())
        log = (self.ci_root / "logs" / SHA_A[:12] / "gate.log").read_text()
        self.assertNotIn(token, log)
        self.assertNotIn(token, (self.ci_root / "logs" / SHA_A[:12] / "summary.json").read_text())
        for _, _, desc in self.gh.statuses:
            self.assertNotIn(token, desc)


class PollLockTest(GateTestBase):
    def test_a_second_poller_exits_at_once(self):
        self.gh.prs = [pr()]
        with gate._poll_lock(self.ci_root / "poll.lock") as got:
            self.assertTrue(got)
            self.assertEqual(gate.poll(self.cfg()), [])
        self.assertEqual(self.shell.calls, [])


class WorktreeLayoutTest(GateTestBase):
    """./aerie refuses a checkout inside the build root, so a gate worktree must not sit in it."""

    def test_the_ci_folder_is_outside_the_build_root(self):
        cfg = self.cfg()
        self.assertFalse(cfg.ci_root.is_relative_to(self.root))
        self.assertFalse(self.root.is_relative_to(cfg.ci_root))
        self.assertEqual(cfg.ci_root, self.root.parent / "build-ci")

    def test_the_ci_folder_can_be_set(self):
        self.assertEqual(self.cfg(ci_dir=Path("/srv/ci")).ci_root, Path("/srv/ci"))

    def test_aerie_accepts_a_gate_worktree_as_its_checkout_for_real(self):
        cfg = self.cfg()
        tree = cfg.ci_root / "worktrees" / SHA_A[:12]
        code = ("import sys; sys.path.insert(0, sys.argv[1]);"
                "from pathlib import Path; from aerie_build import config;"
                "print(config.resolve_build_root(sys.argv[2], {}, Path(sys.argv[3])))")
        tools = str(Path(__file__).resolve().parents[1] / "tools")
        done = subprocess.run([sys.executable, "-I", "-c", code, tools, str(self.root), str(tree)],
                              capture_output=True, text=True)
        self.assertEqual(done.returncode, 0, done.stderr)
        self.assertEqual(done.stdout.strip(), str(self.root))
