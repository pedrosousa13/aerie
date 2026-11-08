import unittest
from pathlib import Path

from aerie_build import config
from aerie_build.config import ConfigError


REPO = Path("/home/someone/aerie")


class ResolveBuildRootTest(unittest.TestCase):
    def test_defaults_to_data_drive(self):
        root = config.resolve_build_root(None, {}, REPO)
        self.assertEqual(root, Path("/mnt/linux2/aerie-build"))

    def test_env_overrides_default(self):
        root = config.resolve_build_root(None, {"AERIE_BUILD_ROOT": "/data/ab"}, REPO)
        self.assertEqual(root, Path("/data/ab"))

    def test_flag_overrides_env(self):
        root = config.resolve_build_root(
            "/flag/root", {"AERIE_BUILD_ROOT": "/data/ab"}, REPO
        )
        self.assertEqual(root, Path("/flag/root"))

    def test_empty_env_falls_back_to_default(self):
        root = config.resolve_build_root(None, {"AERIE_BUILD_ROOT": ""}, REPO)
        self.assertEqual(root, config.DEFAULT_BUILD_ROOT)

    def test_relative_path_is_rejected(self):
        with self.assertRaises(ConfigError):
            config.resolve_build_root("relative/dir", {}, REPO)

    def test_root_inside_repo_is_rejected(self):
        with self.assertRaises(ConfigError):
            config.resolve_build_root(str(REPO / "out"), {}, REPO)

    def test_repo_inside_root_is_rejected(self):
        with self.assertRaises(ConfigError):
            config.resolve_build_root("/home/someone", {}, REPO)

    def test_filesystem_root_is_rejected(self):
        with self.assertRaises(ConfigError):
            config.resolve_build_root("/", {}, REPO)


class CacheSizeTest(unittest.TestCase):
    def test_default_is_30g(self):
        self.assertEqual(config.resolve_cache_size(None, {}), "30G")

    def test_env_and_flag_precedence(self):
        env = {"AERIE_CCACHE_SIZE": "50G"}
        self.assertEqual(config.resolve_cache_size(None, env), "50G")
        self.assertEqual(config.resolve_cache_size("10G", env), "10G")

    def test_accepts_units_case_insensitively(self):
        self.assertEqual(config.resolve_cache_size("512m", {}), "512M")
        self.assertEqual(config.resolve_cache_size("1T", {}), "1T")

    def test_rejects_garbage_and_zero(self):
        for bad in ("", "lots", "30", "0G", "-5G", "30GB", "1.5G"):
            with self.subTest(bad=bad):
                with self.assertRaises(ConfigError):
                    config.resolve_cache_size(bad, {})


class CcacheDirTest(unittest.TestCase):
    def test_default_is_none_so_the_cache_stays_in_the_build_root(self):
        self.assertIsNone(config.resolve_ccache_dir(None, {}))

    def test_env_and_flag_precedence(self):
        env = {"AERIE_CCACHE_DIR": "/mnt/x/cache"}
        self.assertEqual(config.resolve_ccache_dir(None, env), Path("/mnt/x/cache"))
        self.assertEqual(config.resolve_ccache_dir("/mnt/y/cache", env), Path("/mnt/y/cache"))

    def test_must_be_absolute(self):
        with self.assertRaises(ConfigError):
            config.resolve_ccache_dir("cache", {})

    def test_paths_use_it_and_the_env_points_ccache_at_it(self):
        paths = config.BuildPaths(Path("/r/nightly"), ccache_dir=Path("/r/main/ccache"))
        self.assertEqual(paths.ccache, Path("/r/main/ccache"))
        self.assertIn(Path("/r/main/ccache"), paths.scratch_dirs())
        self.assertEqual(config.isolated_env(paths, {})["CCACHE_DIR"], "/r/main/ccache")
        self.assertEqual(paths.src, Path("/r/nightly/src"))  # everything else stays in its own root

    def test_without_it_the_cache_is_in_the_root(self):
        self.assertEqual(config.BuildPaths(Path("/r")).ccache, Path("/r/ccache"))


class BuildPathsTest(unittest.TestCase):
    def test_everything_lives_under_root(self):
        root = Path("/mnt/x/aerie-build")
        paths = config.BuildPaths(root)
        for name in (
            "downloads",
            "src",
            "toolchain",
            "ccache",
            "home",
            "tmp",
            "xdg",
            "logs",
            "pycache",
        ):
            with self.subTest(name=name):
                self.assertTrue(getattr(paths, name).is_relative_to(root))

    def test_tree_and_out_dir(self):
        paths = config.BuildPaths(Path("/r"))
        self.assertEqual(paths.tree("helium-1.2"), Path("/r/src/helium-1.2"))
        self.assertEqual(
            paths.out_dir("helium-1.2"), Path("/r/src/helium-1.2/out/Default")
        )


class IsolatedEnvTest(unittest.TestCase):
    def test_redirects_home_tmp_and_caches_into_root(self):
        root = Path("/r")
        paths = config.BuildPaths(root)
        base = {"HOME": "/home/me", "TMPDIR": "/tmp", "PATH": "/usr/bin", "LANG": "C"}
        env = config.isolated_env(paths, base)
        for key in (
            "HOME",
            "TMPDIR",
            "TMP",
            "TEMP",
            "XDG_CACHE_HOME",
            "XDG_CONFIG_HOME",
            "XDG_DATA_HOME",
            "XDG_STATE_HOME",
            "XDG_RUNTIME_DIR",
            "PYTHONPYCACHEPREFIX",
            "PIP_CACHE_DIR",
            "CCACHE_DIR",
        ):
            with self.subTest(key=key):
                self.assertTrue(Path(env[key]).is_relative_to(root), env[key])
        self.assertEqual(env["PATH"], "/usr/bin")
        self.assertEqual(env["LANG"], "C")
        self.assertEqual(base["HOME"], "/home/me", "base env must not be mutated")

    def test_drops_inherited_ccache_overrides(self):
        paths = config.BuildPaths(Path("/r"))
        env = config.isolated_env(
            paths, {"CCACHE_MAXSIZE": "999G", "CCACHE_CONFIGPATH": "/etc/x"}
        )
        self.assertNotIn("CCACHE_MAXSIZE", env)
        self.assertNotIn("CCACHE_CONFIGPATH", env)

    def test_drops_host_toolchain_settings(self):
        # A host GOROOT (e.g. from mise) breaks Chromium's bundled Go toolchain.
        paths = config.BuildPaths(Path("/r"))
        leaky = {
            "GOROOT": "/home/me/go1.24",
            "GOBIN": "/home/me/go/bin",
            "GOFLAGS": "-x",
            "CC": "gcc",
            "CXX": "g++",
            "CFLAGS": "-O0",
            "LDFLAGS": "-L/x",
            "RUSTFLAGS": "-C x",
            "CARGO_HOME": "/home/me/.cargo",
            "PYTHONPATH": "/home/me/lib",
            "PYTHONHOME": "/opt/py",
            "NODE_OPTIONS": "--x",
            "LD_LIBRARY_PATH": "/opt/lib",
            "PKG_CONFIG_PATH": "/opt/pc",
        }
        env = config.isolated_env(paths, dict(leaky, PATH="/usr/bin"))
        self.assertEqual(sorted(set(leaky) & set(env)), [])
        self.assertEqual(env["PATH"], "/usr/bin")


if __name__ == "__main__":
    unittest.main()
