import tempfile
import unittest
from pathlib import Path
from unittest import mock

from aerie_build.config import ConfigError
from aerie_ci import cli, gate, install

from .ci_fakes import ME, SHA_A, FakeGh, FakeShell, pr


class InstallTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.dest = Path(self._tmp.name) / "systemd" / "user"
        self.repo = Path("/home/x/apps/aerie")

    def test_writes_two_services_and_two_timers_with_the_repo_path(self):
        written = install.install(self.dest, self.repo)
        names = sorted(p.name for p in written)
        self.assertEqual(names, ["aerie-ci-nightly.service", "aerie-ci-nightly.timer",
                                 "aerie-ci-poll.service", "aerie-ci-poll.timer"])
        svc = (self.dest / "aerie-ci-poll.service").read_text()
        self.assertIn("WorkingDirectory=/home/x/apps/aerie", svc)
        self.assertIn("-m tools.aerie_ci poll", svc)
        self.assertNotIn("@REPO@", svc)
        self.assertIn("-m tools.aerie_ci nightly", (self.dest / "aerie-ci-nightly.service").read_text())

    def test_timers_are_user_timers_that_start_their_service(self):
        install.install(self.dest, self.repo)
        poll = (self.dest / "aerie-ci-poll.timer").read_text()
        night = (self.dest / "aerie-ci-nightly.timer").read_text()
        self.assertIn("OnUnitInactiveSec=10min", poll)
        self.assertIn("OnCalendar=", night)
        for t in (poll, night):
            self.assertIn("WantedBy=timers.target", t)
        for name in ("aerie-ci-poll", "aerie-ci-nightly"):  # timer and service share the base name
            self.assertTrue((self.dest / f"{name}.service").is_file())

    def test_install_does_not_start_anything_unless_asked(self):
        shell = FakeShell()
        install.install(self.dest, self.repo, shell=shell)
        self.assertEqual(shell.calls, [])

    def test_enable_reloads_and_enables_the_timers_for_the_user(self):
        shell = FakeShell()
        install.install(self.dest, self.repo, shell=shell, enable=True)
        j = shell.joined()
        self.assertEqual(j[0], "systemctl --user daemon-reload")
        self.assertEqual(j[1], "systemctl --user enable --now aerie-ci-poll.timer aerie-ci-nightly.timer")
        self.assertFalse(any("sudo" in c for c in j))

    def test_only_nightly_enables_just_the_nightly_timer_and_still_writes_every_unit(self):
        shell = FakeShell()
        written = install.install(self.dest, self.repo, shell=shell, enable=True, only=("nightly",))
        self.assertEqual(len(written), 4)  # poll and gate units stay available, off
        self.assertEqual(shell.joined()[1], "systemctl --user enable --now aerie-ci-nightly.timer")
        self.assertFalse(any("poll" in c for c in shell.joined()))

    def test_only_poll_enables_just_the_poll_timer(self):
        shell = FakeShell()
        install.install(self.dest, self.repo, shell=shell, enable=True, only=("poll",))
        self.assertEqual(shell.joined()[1], "systemctl --user enable --now aerie-ci-poll.timer")


    def test_default_destination_is_the_user_systemd_dir(self):
        self.assertEqual(install.default_dest({"HOME": "/home/x"}), Path("/home/x/.config/systemd/user"))
        self.assertEqual(install.default_dest({"HOME": "/h", "XDG_CONFIG_HOME": "/c"}), Path("/c/systemd/user"))


class CliTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.root = Path(self._tmp.name)
        self.gh = FakeGh([pr(3, SHA_A, ME), pr(4, "c" * 40, "mallory")])
        self.shell = FakeShell()
        patcher = mock.patch.object(cli, "_make_config", side_effect=self._config)
        patcher.start()
        self.addCleanup(patcher.stop)

    def _config(self, args):
        return gate.CiConfig(repo=self.root / "repo", build_root=self.root, shell=self.shell, gh=self.gh,
                               python="python3", lock_timeout=0, environ={"PATH": "/usr/bin"})

    def test_poll_gates_the_maintainers_pr_and_exits_zero(self):
        self.assertEqual(cli.main(["poll"]), 0)
        self.assertEqual(self.gh.states(SHA_A)[-1], "success")
        self.assertEqual(self.gh.states("c" * 40), [])

    def test_gate_one_pr_by_number(self):
        self.assertEqual(cli.main(["gate", "3"]), 0)
        self.assertEqual(self.gh.states()[-1], "success")

    def test_gate_refuses_another_authors_pr(self):
        self.assertEqual(cli.main(["gate", "4"]), 1)
        self.assertEqual(self.shell.calls, [])

    def test_gate_unknown_pr(self):
        self.assertEqual(cli.main(["gate", "99"]), 1)

    def test_gate_exit_code_follows_the_result(self):
        self.shell.fail = {"aerie build": 1}
        self.assertEqual(cli.main(["gate", "3"]), 1)

    def test_nightly_exit_code(self):
        self.assertEqual(cli.main(["nightly"]), 0)
        self.shell.fail = {"aerie build": 1}
        self.assertEqual(cli.main(["nightly"]), 1)

    def test_install_command_only_nightly(self):
        dest = self.root / "units"
        with mock.patch.object(install.st.Shell, "capture", return_value="") as cap:
            self.assertEqual(cli.main(["install", "--dest", str(dest), "--enable", "--only", "nightly"]), 0)
        self.assertEqual(cap.call_args_list[-1].args[0][-1], "aerie-ci-nightly.timer")
        self.assertTrue((dest / "aerie-ci-poll.timer").is_file())

    def test_install_only_rejects_an_unknown_name_before_writing_anything(self):
        with self.assertRaises(SystemExit) as ctx, mock.patch("sys.stderr"):
            cli.main(["install", "--dest", str(self.root / "u"), "--enable", "--only", "gate"])
        self.assertEqual(ctx.exception.code, 2)
        self.assertFalse((self.root / "u").exists())

    def test_install_only_without_enable_is_an_error(self):
        self.assertEqual(cli.main(["install", "--dest", str(self.root / "u"), "--only", "nightly"]), 1)

    def test_install_command_writes_units(self):
        dest = self.root / "units"
        self.assertEqual(cli.main(["install", "--dest", str(dest)]), 0)
        self.assertTrue((dest / "aerie-ci-poll.timer").is_file())


class NightlyRootTest(unittest.TestCase):
    MAIN = "/mnt/x/aerie-build"

    def roots(self, argv, environ):
        args = cli.build_parser().parse_args(argv)
        return cli.nightly_roots(args, {"AERIE_BUILD_ROOT": self.MAIN, **environ})

    def test_default_is_a_sibling_root_that_shares_the_main_cache(self):
        root, cache = self.roots(["nightly"], {})
        self.assertEqual(root, Path("/mnt/linux2/aerie-build-nightly"))
        self.assertEqual(cache, Path(self.MAIN) / "ccache")

    def test_the_main_root_environment_is_not_the_nightly_root(self):
        root, _ = self.roots(["nightly"], {})
        self.assertNotEqual(root, Path(self.MAIN))

    def test_flag_beats_env_beats_default(self):
        env = {"AERIE_NIGHTLY_BUILD_ROOT": "/mnt/x/n-env"}
        self.assertEqual(self.roots(["nightly"], env)[0], Path("/mnt/x/n-env"))
        self.assertEqual(self.roots(["nightly", "--build-root", "/mnt/x/n-flag"], env)[0], Path("/mnt/x/n-flag"))

    def test_ccache_dir_flag_and_env(self):
        self.assertEqual(self.roots(["nightly"], {"AERIE_CCACHE_DIR": "/c/e"})[1], Path("/c/e"))
        self.assertEqual(self.roots(["nightly", "--ccache-dir", "/c/f"], {"AERIE_CCACHE_DIR": "/c/e"})[1], Path("/c/f"))

    def test_refuses_the_main_root_or_a_root_inside_it(self):
        for bad in (self.MAIN, self.MAIN + "/nightly"):
            with self.subTest(bad=bad), self.assertRaises(ConfigError):
                self.roots(["nightly", "--build-root", bad], {})

    def test_refuses_a_root_that_contains_the_main_root(self):
        with self.assertRaises(ConfigError):
            self.roots(["nightly", "--build-root", "/mnt/x"], {})
