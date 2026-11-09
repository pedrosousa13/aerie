"""The pinned host list of the adblock_score check: lock, verification, cache-or-skip, parser, pin refresh.

The list text below is synthetic (example names, written for these tests). It follows the layout of the
real list's headers and entries, which is not committed to this repo (its licence is CC BY-NC-SA 4.0).
"""

import hashlib
import io
import json
import tempfile
import unittest
import urllib.error
from pathlib import Path

from aerie_bench import adblock_hosts as ah
from aerie_build import filter_lists

TEXT = """# Title: Synthetic host list for tests
# Entries : 5

#============ Ads =============

# --- Vendor One
0.0.0.0 ads.example.net
0.0.0.0 pixel.example.net

#====== Social Trackers =======
# --- Vendor Two
0.0.0.0 social.example.org
0.0.0.0 ADS.example.net
0.0.0.0 log.example.org # trailing comment
"""
DATA = TEXT.encode()


def pin(data=DATA, url="https://lists.example/x.txt", **kw):
    fields = dict(url=url, sha256=hashlib.sha256(data).hexdigest(), size=len(data), date="2026-11-09")
    fields.update(kw)
    return ah.Pin(**fields)


class FakeResponse(io.BytesIO):
    def __init__(self, data, status=200, headers=None, url=""):
        super().__init__(data)
        self.status, self.headers, self._url = status, headers or {}, url

    def geturl(self):
        return self._url


def serving(data, calls=None):
    def opener(request):
        if calls is not None:
            calls.append(request.full_url)
        return FakeResponse(data, url=request.full_url)
    return opener


def quiet(_message):
    pass


def offline(request):
    raise urllib.error.URLError("no route")


class LockTest(unittest.TestCase):
    def write(self, **kw):
        body = {"schema": 1, "url": "https://lists.example/x.txt", "sha256": "a" * 64, "size": 10,
                "date": "2026-11-09", **kw}
        d = tempfile.mkdtemp()
        self.addCleanup(lambda: __import__("shutil").rmtree(d, ignore_errors=True))
        path = Path(d) / "lock.json"
        path.write_text(json.dumps(body))
        return path

    def test_a_good_lock_loads(self):
        p = ah.load_pin(self.write())
        self.assertEqual((p.url, p.size, p.date), ("https://lists.example/x.txt", 10, "2026-11-09"))

    def test_http_url_is_refused(self):
        with self.assertRaisesRegex(ah.HostListError, "https"):
            ah.load_pin(self.write(url="http://lists.example/x.txt"))

    def test_bad_sha_is_refused(self):
        with self.assertRaisesRegex(ah.HostListError, "sha256"):
            ah.load_pin(self.write(sha256="xyz"))

    def test_bad_size_and_date_are_refused(self):
        with self.assertRaisesRegex(ah.HostListError, "size"):
            ah.load_pin(self.write(size=0))
        with self.assertRaisesRegex(ah.HostListError, "date"):
            ah.load_pin(self.write(date="yesterday"))

    def test_the_committed_lock_is_valid_and_pins_an_immutable_https_url(self):
        p = ah.load_pin()
        self.assertTrue(p.url.startswith("https://"))
        self.assertRegex(p.url, r"/[0-9a-f]{40}/", "pin a commit, not a moving branch")

    def test_the_repo_holds_no_copy_of_the_list(self):
        repo = Path(__file__).resolve().parents[1]
        for path in repo.rglob("*"):
            if ".git" in path.parts or not path.is_file() or path.stat().st_size > 1_000_000:
                continue
            if path.suffix in (".txt", ".json", ".adblock"):
                head = path.read_bytes()[:400]
                self.assertNotIn(b"Turtlecute Host List", head, path)


class VerifyTest(unittest.TestCase):
    def test_exact_bytes_pass(self):
        ah.verify(pin(), DATA)

    def test_bad_hash_is_refused_and_names_both(self):
        with self.assertRaisesRegex(ah.HostListError, "sha256"):
            ah.verify(pin(), DATA + b"# x\n")

    def test_bad_size_is_refused(self):
        with self.assertRaisesRegex(ah.HostListError, "size"):
            ah.verify(pin(size=len(DATA) + 1), DATA)


class ObtainTest(unittest.TestCase):
    def setUp(self):
        self.root = Path(tempfile.mkdtemp())
        self.addCleanup(lambda: __import__("shutil").rmtree(self.root, ignore_errors=True))

    def test_download_is_verified_and_cached_outside_the_repo(self):
        calls = []
        got = ah.obtain(pin(), self.root, serving(DATA, calls))
        self.assertEqual((got.text, got.source), (TEXT, "download"))
        self.assertEqual(calls, ["https://lists.example/x.txt"])
        cached = ah.cache_path(self.root, pin())
        self.assertEqual(cached.read_bytes(), DATA)
        self.assertEqual(cached.parent, self.root / "downloads")

    def test_no_network_uses_a_verified_cache(self):
        ah.obtain(pin(), self.root, serving(DATA))
        got = ah.obtain(pin(), self.root, offline)
        self.assertEqual(got.source, "cache")
        self.assertIn("no route", got.note)

    def test_no_network_and_no_cache_is_unavailable_with_a_reason(self):
        with self.assertRaises(ah.HostListUnavailable) as ctx:
            ah.obtain(pin(), self.root, offline)
        self.assertIn("no route", str(ctx.exception))
        self.assertIn("cache", str(ctx.exception))

    def test_a_cache_that_fails_verification_is_not_used(self):
        path = ah.cache_path(self.root, pin())
        path.parent.mkdir(parents=True)
        path.write_bytes(DATA + b"tampered")
        with self.assertRaises(ah.HostListUnavailable) as ctx:
            ah.obtain(pin(), self.root, offline)
        self.assertIn("does not match", str(ctx.exception))

    def test_a_download_with_a_bad_hash_is_an_error_not_a_skip_and_is_not_cached(self):
        with self.assertRaises(ah.HostListError):
            ah.obtain(pin(), self.root, serving(DATA + b"changed upstream"))
        self.assertFalse(ah.cache_path(self.root, pin()).exists())

    def test_a_bad_download_falls_back_to_a_good_cache_and_says_so(self):
        ah.obtain(pin(), self.root, serving(DATA))
        got = ah.obtain(pin(), self.root, serving(b"garbage"))
        self.assertEqual(got.source, "cache")
        self.assertIn("sha256", got.note)

    def test_a_redirect_to_http_is_refused_as_an_error_even_with_a_cache(self):
        def opener(request):
            return FakeResponse(b"", status=302, headers={"Location": "http://evil.example/x.txt"})
        with self.assertRaises(ah.HostListError) as ctx:
            ah.obtain(pin(), self.root, opener)
        self.assertIn("not https", str(ctx.exception))

    def test_an_http_pin_is_refused_before_any_request(self):
        calls = []
        with self.assertRaises(ah.HostListError):
            ah.obtain(pin(url="http://lists.example/x.txt"), self.root, serving(DATA, calls))
        self.assertEqual(calls, [])

    def test_a_redirect_to_https_is_followed_and_verified(self):
        def opener(request):
            if request.full_url.endswith("/x.txt"):
                return FakeResponse(b"", status=302, headers={"Location": "https://cdn.example/y.txt"})
            return FakeResponse(DATA, url=request.full_url)
        self.assertEqual(ah.obtain(pin(), self.root, opener).source, "download")

    def test_no_build_root_means_no_cache_and_still_downloads(self):
        self.assertEqual(ah.obtain(pin(), None, serving(DATA)).source, "download")
        with self.assertRaises(ah.HostListUnavailable):
            ah.obtain(pin(), None, offline)


class ParseTest(unittest.TestCase):
    def test_categories_hosts_lowercase_deduplicated_in_order(self):
        self.assertEqual(ah.parse(TEXT), {
            "Ads": ["ads.example.net", "pixel.example.net"],
            "Social Trackers": ["social.example.org", "log.example.org"]})

    def test_a_host_that_is_not_a_plain_name_is_refused(self):
        for bad in ("0.0.0.0 *.example.net", "0.0.0.0 a.example.net:8080", "0.0.0.0 evil.example,MAP", "0.0.0.0 localhost",
                    "0.0.0.0 127.0.0.1"):
            with self.subTest(bad=bad):
                with self.assertRaises(ah.HostListError):
                    ah.parse("#== Ads ==\n" + bad + "\n")

    def test_an_empty_list_is_refused(self):
        with self.assertRaises(ah.HostListError):
            ah.parse("# only comments\n")

    def test_entries_before_any_header_get_a_default_category(self):
        self.assertEqual(ah.parse("0.0.0.0 a.example.net\n"), {"Uncategorised": ["a.example.net"]})

    def test_resolver_flag_maps_every_host_to_loopback_before_the_default_refusal(self):
        flag = ah.resolver_flag(["a.example.net", "b.example.org"])
        self.assertTrue(flag.startswith("--host-resolver-rules=MAP a.example.net 127.0.0.1 , MAP b.example.org 127.0.0.1 , "))
        self.assertLess(flag.index("MAP b.example.org"), flag.index("MAP * ~NOTFOUND"))
        self.assertIn("EXCLUDE 127.0.0.1", flag)


class UpdatePinTest(unittest.TestCase):
    COMMIT = "b" * 40

    def setUp(self):
        self.dir = Path(tempfile.mkdtemp())
        self.addCleanup(lambda: __import__("shutil").rmtree(self.dir, ignore_errors=True))
        self.lock = self.dir / "lock.json"
        self.root = self.dir / "root"

    def opener(self, calls):
        api = json.dumps([{"sha": self.COMMIT, "commit": {"committer": {"date": "2026-11-07T20:27:39Z"}}}]).encode()

        def opener(request):
            calls.append(request.full_url)
            if request.full_url.startswith("https://api.github.com/"):
                return FakeResponse(api, url=request.full_url)
            return FakeResponse(DATA, url=request.full_url)
        return opener

    def test_refresh_pins_the_newest_commit_and_writes_lock_and_cache(self):
        calls = []
        new = ah.update_pin(self.lock, self.root, opener=self.opener(calls), today="2026-11-09", log=quiet)
        self.assertIn(self.COMMIT, new.url)
        self.assertEqual(ah.load_pin(self.lock), new)
        self.assertEqual((new.sha256, new.size, new.date), (hashlib.sha256(DATA).hexdigest(), len(DATA), "2026-11-09"))
        self.assertEqual(ah.cache_path(self.root, new).read_bytes(), DATA)
        self.assertTrue(calls[0].startswith("https://api.github.com/"))

    def test_an_explicit_commit_skips_the_lookup(self):
        calls = []
        new = ah.update_pin(self.lock, self.root, commit="c" * 40, opener=self.opener(calls), today="2026-11-09", log=quiet)
        self.assertIn("c" * 40, new.url)
        self.assertEqual(len(calls), 1)

    def test_a_malformed_commit_is_refused(self):
        with self.assertRaises(ah.HostListError):
            ah.update_pin(self.lock, self.root, commit="master", opener=self.opener([]), today="2026-11-09", log=quiet)

    def test_a_list_that_does_not_parse_is_not_pinned(self):
        def opener(request):
            return FakeResponse(b"# nothing\n", url=request.full_url)
        with self.assertRaises(ah.HostListError):
            ah.update_pin(self.lock, self.root, commit="c" * 40, opener=opener, today="2026-11-09", log=quiet)
        self.assertFalse(self.lock.exists())


class PruneTest(unittest.TestCase):
    def test_prune_keeps_the_cached_list_of_the_current_pin(self):
        from aerie_build import cli
        self.assertEqual(cli._adblock_host_cache(), {ah.cache_name(ah.load_pin())})


if __name__ == "__main__":
    unittest.main()
