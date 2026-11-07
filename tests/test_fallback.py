"""Official Chromium tarball fallback: URL probing, checksum checks, build wiring.

No test touches the network: downloads go through a fake opener, and the
Helium host is "blocked" by an opener that raises URLError.
"""

import hashlib
import io
import tempfile
import unittest
import urllib.error
from pathlib import Path
from shutil import rmtree as shutil_rmtree
from unittest import mock

from aerie_build import config, fallback, helium_deps, lock, pipeline

VERSION = "157.0.8103.38"
NAME = f"chromium-{VERSION}.tar.xz"
BASE = "https://commondatastorage.googleapis.com/chromium-browser-official/"
TARBALL = b"pretend this is a 4.5 GB tarball"
SHA = hashlib.sha256(TARBALL).hexdigest()


def hashes_text(name=NAME, sha=SHA):
    return (
        f"md5  {'0' * 32}  {name}\n"
        f"sha1  {'1' * 40}  {name}\n"
        f"sha256  {sha}  {name}\n"
        f"sha512  {'2' * 128}  {name}\n"
    )


def pin(sha=SHA):
    return {"url": BASE + NAME, "hashes_url": BASE + NAME + ".hashes", "sha256": sha}


class FakeOpener:
    """Serves a dict of url -> bytes; any other URL raises URLError. Records requests."""

    def __init__(self, files):
        self.files = files
        self.requested = []

    def __call__(self, request):
        self.requested.append((request.get_method(), request.full_url))
        self.agents = getattr(self, "agents", []) + [request.get_header("User-agent")]
        if request.full_url not in self.files:
            raise urllib.error.URLError("blocked")
        return io.BytesIO(self.files[request.full_url])


def blocked(request):
    raise urllib.error.URLError("host blocked")


class ParseHashesTest(unittest.TestCase):
    def test_reads_the_sha256_line(self):
        self.assertEqual(fallback.parse_hashes(hashes_text(), NAME), SHA)

    def test_published_file_format_with_uppercase_digest(self):
        self.assertEqual(fallback.parse_hashes(hashes_text(sha=SHA.upper()), NAME), SHA)

    def test_missing_sha256_is_an_error(self):
        text = f"md5  {'0' * 32}  {NAME}\n"
        with self.assertRaises(lock.VerificationError):
            fallback.parse_hashes(text, NAME)

    def test_entry_for_another_file_is_an_error(self):
        with self.assertRaises(lock.VerificationError):
            fallback.parse_hashes(hashes_text(name="other.tar.xz"), NAME)

    def test_duplicate_entries_are_an_error(self):
        with self.assertRaises(lock.VerificationError):
            fallback.parse_hashes(hashes_text() + hashes_text(), NAME)

    def test_short_digest_is_an_error(self):
        with self.assertRaises(lock.VerificationError):
            fallback.parse_hashes(hashes_text(sha="abcd"), NAME)

    def test_empty_file_is_an_error(self):
        with self.assertRaises(lock.VerificationError):
            fallback.parse_hashes("", NAME)


class ChooseSourceTest(unittest.TestCase):
    def test_auto_uses_official_when_helium_host_is_blocked(self):
        self.assertEqual(fallback.choose_source("auto", VERSION, blocked), "official")

    def test_auto_uses_helium_when_host_answers(self):
        url = fallback.HELIUM_PROBE_URL.format(version=VERSION)
        opener = FakeOpener({url: b""})
        self.assertEqual(fallback.choose_source("auto", VERSION, opener), "helium")
        self.assertEqual(opener.requested, [("HEAD", url)])

    def test_requests_do_not_use_urllibs_default_user_agent(self):
        # tarballs.imput.net returns 403 to "Python-urllib"; found by a live probe.
        url = fallback.HELIUM_PROBE_URL.format(version=VERSION)
        opener = FakeOpener({url: b""})
        fallback.helium_host_reachable(VERSION, opener)
        self.assertEqual(opener.agents, [fallback.USER_AGENT])
        self.assertNotIn("urllib", fallback.USER_AGENT)

    def test_http_error_counts_as_unavailable(self):
        def not_found(request):
            raise urllib.error.HTTPError(request.full_url, 404, "nf", {}, None)

        self.assertFalse(fallback.helium_host_reachable(VERSION, not_found))

    def test_timeout_counts_as_unavailable(self):
        def slow(request):
            raise TimeoutError()

        self.assertFalse(fallback.helium_host_reachable(VERSION, slow))

    def test_forced_modes_do_not_probe(self):
        self.assertEqual(fallback.choose_source("official", VERSION, None), "official")
        self.assertEqual(fallback.choose_source("helium", VERSION, None), "helium")

    def test_mode_resolution(self):
        self.assertEqual(fallback.resolve_mode(None, {}), "auto")
        self.assertEqual(fallback.resolve_mode(None, {"AERIE_SOURCE": "official"}), "official")
        self.assertEqual(fallback.resolve_mode("helium", {"AERIE_SOURCE": "official"}), "helium")
        with self.assertRaises(fallback.FetchError):
            fallback.resolve_mode("bogus", {})


class FetchOfficialTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.dir = Path(self._tmp.name) / "downloads"
        self.log = lambda message: None

    def files(self, tarball=TARBALL, hashes=None):
        return {BASE + NAME: tarball, BASE + NAME + ".hashes": (hashes or hashes_text()).encode()}

    def test_downloads_and_verifies(self):
        path = fallback.fetch_official(pin(), self.dir, FakeOpener(self.files()), self.log)
        self.assertEqual(path, self.dir / NAME)
        self.assertEqual(path.read_bytes(), TARBALL)
        self.assertEqual(list(self.dir.glob("*.partial")), [])

    def test_tarball_mismatch_fails_closed(self):
        opener = FakeOpener(self.files(tarball=b"tampered"))
        with self.assertRaises(lock.VerificationError):
            fallback.fetch_official(pin(), self.dir, opener, self.log)
        self.assertFalse((self.dir / NAME).exists())
        self.assertTrue((self.dir / (NAME + ".rejected")).is_file())

    def test_published_checksum_differs_from_pin_downloads_nothing(self):
        opener = FakeOpener(self.files(hashes=hashes_text(sha="f" * 64)))
        with self.assertRaises(lock.VerificationError):
            fallback.fetch_official(pin(), self.dir, opener, self.log)
        self.assertNotIn(("GET", BASE + NAME), opener.requested)
        self.assertFalse((self.dir / NAME).exists())

    def test_pin_wrong_fails_closed(self):
        with self.assertRaises(lock.VerificationError):
            fallback.fetch_official(pin(sha="e" * 64), self.dir, FakeOpener(self.files()), self.log)
        self.assertFalse((self.dir / NAME).exists())

    def test_unparsable_hashes_file_fails_closed(self):
        opener = FakeOpener(self.files(hashes="<html>404</html>"))
        with self.assertRaises(lock.VerificationError):
            fallback.fetch_official(pin(), self.dir, opener, self.log)
        self.assertNotIn(("GET", BASE + NAME), opener.requested)

    def test_missing_hashes_file_fails_closed(self):
        opener = FakeOpener({BASE + NAME: TARBALL})
        with self.assertRaises(fallback.FetchError):
            fallback.fetch_official(pin(), self.dir, opener, self.log)
        self.assertFalse((self.dir / NAME).exists())

    def test_download_failure_leaves_no_file(self):
        opener = FakeOpener({BASE + NAME + ".hashes": hashes_text().encode()})
        with self.assertRaises(fallback.FetchError):
            fallback.fetch_official(pin(), self.dir, opener, self.log)
        self.assertEqual(list(self.dir.iterdir()), [])

    def test_cached_tarball_is_verified_again(self):
        self.dir.mkdir(parents=True)
        (self.dir / NAME).write_bytes(b"corrupt cache")
        with self.assertRaises(lock.VerificationError):
            fallback.fetch_official(pin(), self.dir, FakeOpener({}), self.log)
        self.assertTrue((self.dir / (NAME + ".rejected")).is_file())

    def test_cached_good_tarball_needs_no_network(self):
        self.dir.mkdir(parents=True)
        (self.dir / NAME).write_bytes(TARBALL)
        opener = FakeOpener({})
        fallback.fetch_official(pin(), self.dir, opener, self.log)
        self.assertEqual(opener.requested, [])

    def test_non_https_url_is_refused(self):
        bad = dict(pin(), url="http://example.invalid/" + NAME)
        with self.assertRaises(fallback.FetchError):
            fallback.fetch_official(bad, self.dir, FakeOpener(self.files()), self.log)


class BuildFallbackTest(unittest.TestCase):
    """fetch_sources with Helium's host blocked, everything slow mocked out."""

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        root = Path(self._tmp.name)
        repo = Path(__file__).resolve().parent.parent
        self.pins = lock.load_lock(repo / "helium.lock")
        self.paths = config.BuildPaths(root / "build")
        self.paths.downloads.mkdir(parents=True)
        # Helium's deps.ini is not in this fake repo; the filtered copy is only named, not read.
        patcher = mock.patch.object(helium_deps, "write_filtered", lambda ini, dest: dest)
        patcher.start()
        self.addCleanup(patcher.stop)
        self.files = {
            self.pins.fallback["url"]: TARBALL,
            self.pins.fallback["hashes_url"]: hashes_text(
                name=Path(self.pins.fallback["url"]).name, sha=self.pins.fallback["sha256"]
            ).encode(),
        }

    def ctx(self, mode, opener):
        return pipeline.Context(
            repo=Path(self._tmp.name) / "repo",
            paths=self.paths,
            pins=self.pins,
            cache_size="1G",
            jobs=None,
            log_path=Path(self._tmp.name) / "log",
            source_mode=mode,
            opener=opener,
        )

    def run_fetch(self, ctx, tar_ok=True):
        log = mock.Mock()
        with mock.patch.object(pipeline, "_verify_downloads") as downloads, mock.patch.object(
            pipeline, "_unpack"
        ), mock.patch.object(pipeline, "run") as run, mock.patch.object(
            pipeline.shutil, "which", return_value="/usr/bin/xz"
        ):
            pipeline.fetch_sources(ctx, log, {})
        return downloads, run, log

    def test_blocked_helium_host_fetches_official_tarball(self):
        # Pin the sha of our fake tarball bytes so the published checksum matches.
        self.pins = lock.Lock(**{**self.pins.__dict__, "fallback": dict(self.pins.fallback, sha256=SHA)})
        self.files[self.pins.fallback["hashes_url"]] = hashes_text(
            name=Path(self.pins.fallback["url"]).name
        ).encode()

        class HostBlocked(FakeOpener):
            def __call__(self, request):
                if "tarballs.imput.net" in request.full_url:
                    self.requested.append((request.get_method(), request.full_url))
                    raise urllib.error.URLError("blocked")
                return super().__call__(request)

        opener = HostBlocked(self.files)
        ctx = self.ctx("auto", opener)
        downloads, run, log = self.run_fetch(ctx)
        name = Path(self.pins.fallback["url"]).name
        self.assertTrue((self.paths.downloads / name).is_file())
        tar_cmd = run.call_args.args[1]
        self.assertEqual(tar_cmd[0], "tar")
        self.assertIn("--strip-components=1", tar_cmd)
        # Only deps.ini is fetched the normal way; Helium's tarball INIs are skipped.
        self.assertEqual(downloads.call_count, 1)
        self.assertEqual([p.name for p in downloads.call_args.args[3]], ["deps.ini"])
        self.assertEqual((ctx.tree / fallback.SOURCE_MARKER).read_text().strip(), "official")
        self.assertTrue((ctx.tree / ".downloaded.stamp").exists())
        self.assertFalse(any("imput.net" in url and m == "GET" for m, url in opener.requested))

    def test_checksum_mismatch_stops_the_build_before_unpacking(self):
        self.files[self.pins.fallback["url"]] = b"tampered"
        self.files[self.pins.fallback["hashes_url"]] = hashes_text(
            name=Path(self.pins.fallback["url"]).name, sha=self.pins.fallback["sha256"]
        ).encode()
        ctx = self.ctx("official", FakeOpener(self.files))
        with self.assertRaises(lock.VerificationError):
            self.run_fetch(ctx)
        self.assertFalse((ctx.tree / ".downloaded.stamp").exists())

    def test_reachable_helium_host_uses_helium_tarballs(self):
        probe = fallback.HELIUM_PROBE_URL.format(version=self.pins.chromium_version)
        ctx = self.ctx("auto", FakeOpener({probe: b""}))
        downloads, run, _ = self.run_fetch(ctx)
        self.assertEqual(downloads.call_count, 2)
        run.assert_not_called()
        self.assertEqual((ctx.tree / fallback.SOURCE_MARKER).read_text().strip(), "helium")

    def stamped_tree(self, kind):
        ctx = self.ctx("auto", blocked)
        ctx.tree.mkdir(parents=True)
        (ctx.tree / ".downloaded.stamp").touch()
        if kind:
            (ctx.tree / fallback.SOURCE_MARKER).write_text(kind + "\n")
        return ctx

    def test_forced_source_that_differs_from_the_unpacked_tree_is_refused(self):
        for kind, mode in (("helium", "official"), ("official", "helium")):
            with self.subTest(kind=kind, mode=mode):
                ctx = self.stamped_tree(kind)
                ctx.source_mode = mode
                with self.assertRaises(pipeline.BuildError) as raised:
                    self.run_fetch(ctx)
                self.assertIn("--clean", str(raised.exception))
                self.assertIn(kind, str(raised.exception))
                self.assertTrue((ctx.tree / ".downloaded.stamp").exists())
                shutil_rmtree(ctx.tree)

    def test_matching_or_auto_source_reuses_the_unpacked_tree(self):
        for kind, mode in (("helium", "helium"), ("official", "official"), ("official", "auto"), ("helium", "auto")):
            with self.subTest(kind=kind, mode=mode):
                ctx = self.stamped_tree(kind)
                ctx.source_mode = mode
                downloads, run, _ = self.run_fetch(ctx)
                # An unpacked tree is verified again, never unpacked again.
                names = [[p.name for p in c.args[3]] for c in downloads.call_args_list]
                expected = [["downloads.ini", "downloads.ini"], ["deps.ini"]] if kind == "helium" else [["deps.ini"]]
                self.assertEqual(names, expected)
                shutil_rmtree(ctx.tree)

    def test_tree_from_before_the_marker_counts_as_helium(self):
        ctx = self.stamped_tree(None)
        ctx.source_mode = "official"
        with self.assertRaises(pipeline.BuildError):
            self.run_fetch(ctx)

    def test_auto_falls_back_when_helium_download_fails_after_a_good_probe(self):
        self.pins = lock.Lock(**{**self.pins.__dict__, "fallback": dict(self.pins.fallback, sha256=SHA)})
        name = Path(self.pins.fallback["url"]).name
        files = {
            fallback.HELIUM_PROBE_URL.format(version=self.pins.chromium_version): b"",
            self.pins.fallback["url"]: TARBALL,
            self.pins.fallback["hashes_url"]: hashes_text(name=name).encode(),
        }
        ctx = self.ctx("auto", FakeOpener(files))
        log = mock.Mock()
        calls = []

        def downloads(ctx_, log_, env_, inis):
            calls.append([p.name for p in inis])
            if "downloads.ini" in calls[-1]:
                (ctx_.tree / "half-unpacked").touch()
                raise pipeline.BuildError("curl failed")

        with mock.patch.object(pipeline, "_verify_downloads", side_effect=downloads), mock.patch.object(
            pipeline, "_unpack"
        ), mock.patch.object(pipeline, "run"), mock.patch.object(pipeline.shutil, "which", return_value="/usr/bin/xz"):
            pipeline.fetch_sources(ctx, log, {})
        self.assertEqual(calls, [["downloads.ini", "downloads.ini"], ["deps.ini"]])
        self.assertTrue((self.paths.downloads / name).is_file())
        self.assertFalse((ctx.tree / "half-unpacked").exists())
        self.assertEqual((ctx.tree / fallback.SOURCE_MARKER).read_text().strip(), "official")

    def test_forced_helium_source_does_not_fall_back_when_download_fails(self):
        ctx = self.ctx("helium", blocked)
        with mock.patch.object(pipeline, "_verify_downloads", side_effect=pipeline.BuildError("curl failed")):
            with self.assertRaises(pipeline.BuildError):
                pipeline.fetch_sources(ctx, mock.Mock(), {})

    def test_configure_refuses_official_tree_without_toolchain(self):
        ctx = self.ctx("official", blocked)
        ctx.tree.mkdir(parents=True)
        (ctx.tree / fallback.SOURCE_MARKER).write_text("official\n")
        with self.assertRaises(pipeline.BuildError) as raised:
            pipeline.configure(ctx, mock.Mock(), {})
        self.assertIn("buildtools/linux64/gn", str(raised.exception))
        self.assertIn("docs/helium-fallback.md", str(raised.exception))

    def test_missing_toolchain_lists_only_what_is_absent(self):
        tree = Path(self._tmp.name) / "tree"
        gn = tree / "buildtools/linux64/gn"
        gn.parent.mkdir(parents=True)
        gn.touch()
        self.assertNotIn("buildtools/linux64/gn", fallback.missing_toolchain(tree))
        self.assertIn("third_party/siso/cipd/siso", fallback.missing_toolchain(tree))


class RepoFallbackLockTest(unittest.TestCase):
    def test_lock_pins_the_official_tarball_for_the_locked_chromium_version(self):
        repo = Path(__file__).resolve().parent.parent
        pins = lock.load_lock(repo / "helium.lock")
        name = f"chromium-{pins.chromium_version}.tar.xz"
        self.assertTrue(pins.fallback["url"].endswith("/chromium-browser-official/" + name))
        self.assertEqual(pins.fallback["hashes_url"], pins.fallback["url"] + ".hashes")


if __name__ == "__main__":
    unittest.main()
