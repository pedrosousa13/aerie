"""The pinned Speedometer and JetStream copies (issue #16): pins, fetch, verify, unpack, serve."""

import hashlib
import io
import tarfile
import tempfile
import unittest
import urllib.error
import urllib.request
from pathlib import Path

from aerie_bench import benchmarks, fixtures
from aerie_build import lock

REPO = Path(__file__).resolve().parent.parent


def make_archive(files: dict[str, bytes], top="Speedometer-abc") -> bytes:
    buf = io.BytesIO()
    with tarfile.open(fileobj=buf, mode="w:gz") as tar:
        for name, data in files.items():
            info = tarfile.TarInfo(f"{top}/{name}")
            info.size = len(data)
            tar.addfile(info, io.BytesIO(data))
    return buf.getvalue()


class FakeResponse(io.BytesIO):
    pass


class PinsTest(unittest.TestCase):
    def test_the_repo_pins_both_benchmarks_with_hash_commit_and_licence(self):
        pins = benchmarks.load_pins(REPO / "helium.lock")
        self.assertEqual(set(pins), {"speedometer", "jetstream"})
        for pin in pins.values():
            self.assertRegex(pin.sha256, r"^[0-9a-f]{64}$")
            self.assertRegex(pin.commit, r"^[0-9a-f]{40}$")
            self.assertTrue(pin.url.startswith("https://github.com/WebKit/"))
            self.assertIn(pin.commit, pin.url)
            self.assertIn("BSD", pin.license)

    def test_every_pinned_benchmark_has_a_driver_and_the_lock_loader_agrees(self):
        pins = lock.load_lock(REPO / "helium.lock").bench
        self.assertEqual(set(pins), set(benchmarks.DRIVERS))
        for driver in benchmarks.DRIVERS.values():
            for key in ("entry", "done", "score", "timeout_s"):
                self.assertTrue(driver[key])

    def test_pinned_archives_are_kept_by_prune(self):
        pins = benchmarks.load_pins(REPO / "helium.lock")
        self.assertEqual(benchmarks.archive_names(pins),
                         {p.archive for p in pins.values()})

    def test_aerie_prune_keeps_the_pinned_archives_and_removes_other_downloads(self):
        import contextlib
        from aerie_build import cli
        root = Path(tempfile.mkdtemp())
        self.addCleanup(lambda: __import__("shutil").rmtree(root, ignore_errors=True))
        (root / "downloads").mkdir()
        pins = benchmarks.load_pins(REPO / "helium.lock")
        for pin in pins.values():
            (root / "downloads" / pin.archive).write_bytes(b"x")
        (root / "downloads" / "stale-thing.tar.gz").write_bytes(b"x")
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            cli.main(["prune", "--build-root", str(root), "-n"])
        for pin in pins.values():
            self.assertNotIn(pin.archive, out.getvalue())
        self.assertIn("stale-thing.tar.gz", out.getvalue())

    def test_a_bad_digest_in_the_lock_is_refused(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "helium.lock"
            text = (REPO / "helium.lock").read_text().replace(
                benchmarks.load_pins(REPO / "helium.lock")["jetstream"].sha256, "abc")
            path.write_text(text)
            with self.assertRaises(lock.VerificationError):
                lock.load_lock(path)


class FetchTest(unittest.TestCase):
    def setUp(self):
        self.root = Path(tempfile.mkdtemp())
        self.addCleanup(lambda: __import__("shutil").rmtree(self.root, ignore_errors=True))
        self.data = make_archive({"index.html": b"<title>x</title>", "resources/a.js": b"1"})
        self.pin = benchmarks.Pin(name="speedometer", title="Speedometer 3.1", url="https://example.invalid/a.tar.gz",
                                  archive="speedometer-test.tar.gz", commit="a" * 40,
                                  sha256=hashlib.sha256(self.data).hexdigest(), license="BSD")
        self.calls = []

    def opener(self, payload=None):
        def open_(request):
            self.calls.append(request.full_url)
            return FakeResponse(self.data if payload is None else payload)
        return open_

    def test_fetch_downloads_verifies_and_unpacks_under_the_build_root(self):
        path = benchmarks.fetch(self.pin, self.root, opener=self.opener(), log=lambda _m: None)
        self.assertEqual(path, self.root / "bench-vendor" / "speedometer")
        self.assertEqual((path / "index.html").read_bytes(), b"<title>x</title>")
        self.assertTrue((path / "resources" / "a.js").is_file())
        self.assertTrue((self.root / "downloads" / "speedometer-test.tar.gz").is_file())

    def test_a_second_fetch_downloads_nothing(self):
        benchmarks.fetch(self.pin, self.root, opener=self.opener(), log=lambda _m: None)
        benchmarks.fetch(self.pin, self.root, opener=self.opener(), log=lambda _m: None)
        self.assertEqual(len(self.calls), 1)

    def test_a_download_with_the_wrong_hash_is_rejected_and_never_unpacked(self):
        other = make_archive({"index.html": b"evil"})
        with self.assertRaises(lock.VerificationError):
            benchmarks.fetch(self.pin, self.root, opener=self.opener(other), log=lambda _m: None)
        self.assertFalse((self.root / "bench-vendor" / "speedometer").exists())
        self.assertTrue((self.root / "downloads" / "speedometer-test.tar.gz.rejected").is_file())
        self.assertFalse((self.root / "downloads" / "speedometer-test.tar.gz").exists())

    def test_a_downloaded_archive_that_was_tampered_with_is_checked_again(self):
        (self.root / "downloads").mkdir()
        (self.root / "downloads" / "speedometer-test.tar.gz").write_bytes(b"tampered")
        with self.assertRaises(lock.VerificationError):
            benchmarks.fetch(self.pin, self.root, opener=self.opener(), log=lambda _m: None)
        self.assertEqual(self.calls, [])

    def test_a_copy_unpacked_from_another_pin_is_replaced(self):
        benchmarks.fetch(self.pin, self.root, opener=self.opener(), log=lambda _m: None)
        newer = make_archive({"index.html": b"new"})
        pin = benchmarks.Pin(**{**self.pin.__dict__, "sha256": hashlib.sha256(newer).hexdigest(),
                                "archive": "speedometer-new.tar.gz", "commit": "b" * 40})
        benchmarks.fetch(pin, self.root, opener=self.opener(newer), log=lambda _m: None)
        self.assertEqual((self.root / "bench-vendor" / "speedometer" / "index.html").read_bytes(), b"new")
        self.assertFalse((self.root / "bench-vendor" / "speedometer" / "resources").exists())

    def test_an_archive_member_that_escapes_the_directory_is_refused(self):
        buf = io.BytesIO()
        with tarfile.open(fileobj=buf, mode="w:gz") as tar:
            info = tarfile.TarInfo("top/../../escape.txt")
            info.size = 1
            tar.addfile(info, io.BytesIO(b"x"))
        data = buf.getvalue()
        pin = benchmarks.Pin(**{**self.pin.__dict__, "sha256": hashlib.sha256(data).hexdigest()})
        with self.assertRaises(benchmarks.BenchmarkError):
            benchmarks.fetch(pin, self.root, opener=self.opener(data), log=lambda _m: None)
        self.assertFalse((self.root / "escape.txt").exists())
        self.assertFalse((self.root / "bench-vendor" / "speedometer").exists())

    def test_non_https_urls_are_refused(self):
        pin = benchmarks.Pin(**{**self.pin.__dict__, "url": "http://example.invalid/a.tar.gz"})
        with self.assertRaises(benchmarks.BenchmarkError):
            benchmarks.fetch(pin, self.root, opener=self.opener(), log=lambda _m: None)

    def test_a_network_failure_says_so(self):
        def boom(_request):
            raise urllib.error.URLError("no route")
        with self.assertRaises(benchmarks.BenchmarkError):
            benchmarks.fetch(self.pin, self.root, opener=boom, log=lambda _m: None)

    def test_present_reports_only_a_copy_that_matches_the_pin(self):
        self.assertFalse(benchmarks.present(self.pin, self.root))
        benchmarks.fetch(self.pin, self.root, opener=self.opener(), log=lambda _m: None)
        self.assertTrue(benchmarks.present(self.pin, self.root))
        other = benchmarks.Pin(**{**self.pin.__dict__, "commit": "c" * 40})
        self.assertFalse(benchmarks.present(other, self.root))


class ServeTest(unittest.TestCase):
    def test_vendor_files_come_from_the_build_root_and_nothing_escapes(self):
        outer = Path(tempfile.mkdtemp())
        self.addCleanup(lambda: __import__("shutil").rmtree(outer, ignore_errors=True))
        root = outer / "bench-vendor"
        (root / "speedometer").mkdir(parents=True)
        (root / "speedometer" / "index.html").write_text("<title>vendored</title>")
        (outer / "secret.txt").write_text("no")
        with fixtures.FixtureServer(vendor_root=root) as srv:
            self.assertIn(b"vendored", urllib.request.urlopen(f"{srv.base_url}/vendor/speedometer/").read())
            for path in ("/vendor/../secret.txt", "/vendor/speedometer/../../secret.txt", "/vendor/secret.txt"):
                with self.assertRaises(urllib.error.HTTPError, msg=path):
                    urllib.request.urlopen(f"{srv.base_url}{path}")

    def test_without_a_vendor_root_nothing_is_served_under_vendor(self):
        with fixtures.FixtureServer() as srv:
            with self.assertRaises(urllib.error.HTTPError):
                urllib.request.urlopen(f"{srv.base_url}/vendor/speedometer/index.html")


if __name__ == "__main__":
    unittest.main()
