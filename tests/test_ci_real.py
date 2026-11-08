"""CI root and cache options with real temp dirs and real child processes; nothing is mocked."""

import os
import stat
import subprocess
import sys
import tempfile
import tomllib
import unittest
from pathlib import Path

from aerie_build.config import ConfigError
from aerie_ci import cli

REPO = Path(__file__).resolve().parents[1]

_FAKE_CCACHE = """#!/bin/sh
# Stands in for ccache: records what a real ccache child would see.
{ echo "CCACHE_DIR=$CCACHE_DIR"; echo "CCACHE_BASEDIR=$CCACHE_BASEDIR"; echo "ARGS=$*"; } > "$FAKE_CCACHE_OUT"
"""


class CcacheOptionReachesTheChildTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.tmp = Path(self._tmp.name)
        self.root = self.tmp / "nightly"
        self.cache = self.tmp / "main" / "ccache"
        version = tomllib.loads((REPO / "helium.lock").read_text())["tools"]["ccache"]["version"]
        fake = self.root / "toolchain" / f"ccache-{version}" / "ccache"
        fake.parent.mkdir(parents=True)
        fake.write_text(_FAKE_CCACHE)
        fake.chmod(fake.stat().st_mode | stat.S_IXUSR)
        self.out = self.tmp / "seen.txt"

    def prune(self, *extra, env=None):
        base = {k: v for k, v in os.environ.items() if not k.startswith("AERIE_")}
        return subprocess.run(
            [sys.executable, str(REPO / "aerie"), "prune", "--build-root", str(self.root), *extra],
            env={**base, "FAKE_CCACHE_OUT": str(self.out), **(env or {})}, capture_output=True, text=True)

    def seen(self) -> dict:
        return dict(line.split("=", 1) for line in self.out.read_text().splitlines())

    def test_ccache_dir_flag_reaches_ccache_in_a_real_child(self):
        done = self.prune("--ccache-dir", str(self.cache))
        self.assertEqual(done.returncode, 0, done.stderr)
        seen = self.seen()
        self.assertEqual(seen["CCACHE_DIR"], str(self.cache))
        self.assertEqual(seen["CCACHE_BASEDIR"], str(self.root / "src"))
        self.assertEqual(seen["ARGS"], "--cleanup")
        self.assertIn("hash_dir = false", (self.cache / "ccache.conf").read_text())
        self.assertFalse((self.root / "ccache").exists())  # the root's own cache folder is not used

    def test_the_environment_variable_does_the_same(self):
        done = self.prune(env={"AERIE_CCACHE_DIR": str(self.cache)})
        self.assertEqual(done.returncode, 0, done.stderr)
        self.assertEqual(self.seen()["CCACHE_DIR"], str(self.cache))

    def test_without_either_the_cache_is_in_the_root(self):
        self.assertEqual(self.prune().returncode, 0)
        self.assertEqual(self.seen()["CCACHE_DIR"], str(self.root / "ccache"))


class NightlyRootOnRealDirsTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.tmp = Path(self._tmp.name).resolve()
        self.main = self.tmp / "aerie-build"
        self.main.mkdir()

    def roots(self, *argv, env=None):
        args = cli.build_parser().parse_args(["nightly", *argv])
        return cli.nightly_roots(args, {"AERIE_BUILD_ROOT": str(self.main), **(env or {})})

    def test_a_sibling_root_is_accepted_and_shares_the_main_cache(self):
        night = self.tmp / "aerie-build-nightly"
        root, cache = self.roots("--build-root", str(night))
        self.assertEqual((root, cache), (night, self.main / "ccache"))

    def test_a_root_inside_or_around_the_main_root_is_refused(self):
        (self.main / "sub").mkdir()
        for bad in (self.main, self.main / "sub", self.tmp):
            with self.subTest(bad=str(bad)), self.assertRaises(ConfigError):
                self.roots("--build-root", str(bad))

    def test_a_symlink_to_the_main_root_is_refused(self):
        alias = self.tmp / "alias"
        alias.symlink_to(self.main)
        with self.assertRaises(ConfigError):
            self.roots("--build-root", str(alias))
        with self.assertRaises(ConfigError):
            self.roots("--build-root", str(alias / "inner"))

    def test_the_main_root_given_through_a_symlink_is_still_the_main_root(self):
        alias = self.tmp / "alias"
        alias.symlink_to(self.main)
        with self.assertRaises(ConfigError):
            self.roots("--build-root", str(self.main), env={"AERIE_BUILD_ROOT": str(alias)})


if __name__ == "__main__":
    unittest.main()
