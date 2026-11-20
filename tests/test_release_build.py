"""./aerie build --release: its GN arguments are the ones docs/release-build.md lists, and its PGO profile is pinned."""

import hashlib
import re
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from aerie_build import config, helium, lock, pipeline, release

REPO = Path(__file__).resolve().parent.parent
DOC = REPO / "docs" / "release-build.md"
PROFILE = "chrome-linux-1-2-3-4.profdata"


def documented_args() -> dict[str, str]:
    """The `| `name` | `value` |` rows of the GN arguments table in the docs."""
    text = DOC.read_text(encoding="utf-8")
    section = text.split("## GN arguments", 1)[1].split("\n## ", 1)[0]
    rows = re.findall(r"^\| `([a-z0-9_]+)` \| `(.*)` \|$", section, re.MULTILINE)
    return dict(rows)


def effective(args: str) -> dict[str, str]:
    """GN semantics: the last assignment of a name wins."""
    values = {}
    for line in args.splitlines():
        name, sep, value = line.partition("=")
        if sep:
            values[name.strip()] = value.strip()
    return values


class ReleaseGnArgsTest(unittest.TestCase):
    def setUp(self):
        self.tree = Path("/src/helium-1")
        self.pin = {"name": PROFILE, "sha256": "0" * 64}

    def test_the_preset_sets_exactly_the_arguments_the_docs_list(self):
        expected = {
            name: value.replace("<tree>", str(self.tree)).replace("<profile>", PROFILE)
            for name, value in documented_args().items()
        }
        self.assertTrue(expected, "no GN argument rows found in docs/release-build.md")
        self.assertEqual(effective(release.gn_args(self.tree, self.pin)), expected)

    def test_the_documented_arguments_are_the_full_optimisation_set(self):
        # Independent of the code: what the issue asks for (official build, PGO phase 2, ThinLTO, builtins PGO).
        docs = documented_args()
        for name in ("is_official_build", "use_thin_lto", "v8_enable_builtins_optimization"):
            self.assertEqual(docs[name], "true", name)
        self.assertEqual(docs["chrome_pgo_phase"], "2")
        self.assertIn("pgo_data_path", docs)

    def test_the_release_arguments_override_helium_flags(self):
        base = helium.gn_args("chrome_pgo_phase=0\nsafe_browsing_mode=0\nis_official_build=false\n", "", "x64", None)
        combined = effective(base + release.gn_args(self.tree, self.pin))
        self.assertEqual(combined["chrome_pgo_phase"], "2")
        self.assertEqual(combined["is_official_build"], "true")
        self.assertEqual(combined["safe_browsing_mode"], "0")

    def test_release_has_its_own_output_directory(self):
        paths = config.BuildPaths(Path("/root"))
        self.assertEqual(paths.out_dir("t", release=True), Path("/root/src/t/out/Release"))
        self.assertEqual(paths.out_dir("t"), Path("/root/src/t/out/Default"))


class PinnedProfileTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.tree = Path(self._tmp.name) / "tree"
        self.profiles = self.tree / "chrome" / "build" / "pgo_profiles"
        self.profiles.mkdir(parents=True)
        self.data = b"profile bytes"
        (self.profiles / PROFILE).write_bytes(self.data)
        (self.tree / "chrome" / "build" / "linux.pgo.txt").write_text(PROFILE + "\n")
        self.pin = {"name": PROFILE, "sha256": hashlib.sha256(self.data).hexdigest()}

    def test_the_lock_pins_a_profile_name_and_sha256(self):
        pins = lock.load_lock(REPO / "helium.lock")
        self.assertRegex(pins.pgo["linux"]["name"], r"^chrome-linux-.*\.profdata$")
        self.assertRegex(pins.pgo["linux"]["sha256"], r"^[0-9a-f]{64}$")

    def test_a_matching_profile_is_accepted(self):
        self.assertEqual(release.verify_profile(self.tree, self.pin), self.profiles / PROFILE)

    def test_a_changed_profile_is_refused(self):
        (self.profiles / PROFILE).write_bytes(b"other bytes")
        with self.assertRaisesRegex(release.ProfileError, "sha256"):
            release.verify_profile(self.tree, self.pin)

    def test_a_missing_profile_is_refused(self):
        (self.profiles / PROFILE).unlink()
        with self.assertRaisesRegex(release.ProfileError, "missing"):
            release.verify_profile(self.tree, self.pin)

    def test_a_profile_chromium_would_pick_but_the_lock_does_not_pin_is_refused(self):
        (self.tree / "chrome" / "build" / "linux.pgo.txt").write_text("chrome-linux-9-9-9-9.profdata\n")
        with self.assertRaisesRegex(release.ProfileError, "linux.pgo.txt"):
            release.verify_profile(self.tree, self.pin)

    def test_no_pin_in_the_lock_is_refused(self):
        with self.assertRaisesRegex(release.ProfileError, "helium.lock"):
            release.verify_profile(self.tree, {})

    def test_the_release_configure_stops_before_gn_when_the_profile_does_not_match(self):
        (self.profiles / PROFILE).write_bytes(b"other bytes")
        paths = config.BuildPaths(Path(self._tmp.name) / "root")
        repo = Path(self._tmp.name) / "repo"
        (repo / "helium-chromium").mkdir(parents=True)
        (repo / "helium-linux").mkdir(parents=True)
        (repo / "helium-chromium" / "flags.gn").write_text("safe_browsing_mode=0\n")
        (repo / "helium-linux" / "flags.linux.gn").write_text("")
        pins = lock.Lock("0.1", "1.0", "t", "c", "t", "c", tools={"ccache": {"version": "1"}}, pgo={"linux": self.pin})
        ctx = pipeline.Context(repo=repo, paths=paths, pins=pins, cache_size="1G", jobs=None,
                               log_path=paths.logs / "t.log", release=True)
        with mock.patch.object(pipeline.Context, "tree", new_callable=mock.PropertyMock, return_value=self.tree), \
                mock.patch.object(pipeline, "run") as run:
            with self.assertRaisesRegex(pipeline.BuildError, "sha256"):
                pipeline.configure(ctx, mock.Mock(), {})
        run.assert_not_called()


if __name__ == "__main__":
    unittest.main()
