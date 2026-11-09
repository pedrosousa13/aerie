"""Built-in filter lists: lock, fetch, verification. Nothing here touches the network."""

import hashlib
import io
import json
import tempfile
import unittest
import urllib.error
from pathlib import Path

from aerie_build import filter_lists as fl

REPO = Path(__file__).resolve().parents[1]
URL_A = "https://lists.example/a.txt"
URL_B = "https://lists.example/b.txt"


def list_bytes(title, version, body="||ads.example^\n"):
    return f"[Adblock Plus 2.0]\n! Version: {version}\n! Title: {title}\n{body}".encode()


A = list_bytes("List A", "100")
B = list_bytes("List B", "200", "||tracker.example^\n")


def pin(name, file, url, data, version):
    return fl.ListPin(name, file, url, hashlib.sha256(data).hexdigest(), len(data), version)


PINS = [pin("List A", "a.txt", URL_A, A, "100"), pin("List B", "b.txt", URL_B, B, "200")]


class Opener:
    def __init__(self, files):
        self.files = files
        self.requested = []

    def __call__(self, request):
        self.requested.append(request.full_url)
        if request.full_url not in self.files:
            raise urllib.error.URLError("blocked")
        return io.BytesIO(self.files[request.full_url])


class Redirect(io.BytesIO):
    """A 3xx answer from a fake opener that does not follow redirects."""

    def __init__(self, location, status=302):
        super().__init__(b"")
        self.status = status
        self.headers = {"Location": location}


class RedirectingOpener:
    def __init__(self, answers):
        self.answers = answers
        self.requested = []

    def __call__(self, request):
        self.requested.append(request.full_url)
        answer = self.answers[request.full_url]
        return answer() if callable(answer) else (io.BytesIO(answer) if isinstance(answer, bytes) else answer)


class DownloadRedirectTest(unittest.TestCase):
    def test_https_to_http_is_refused(self):
        opener = RedirectingOpener({URL_A: Redirect("http://evil.example/a.txt")})
        with self.assertRaisesRegex(fl.ListError, "not https"):
            fl._download(URL_A, opener)
        self.assertEqual(opener.requested, [URL_A])  # the http URL is never requested

    def test_https_to_https_is_followed_and_the_final_url_returned(self):
        final = "https://cdn.example/a.txt"
        opener = RedirectingOpener({URL_A: Redirect(final, 301), final: A})
        data, final_url = fl._download(URL_A, opener)
        self.assertEqual((data, final_url), (A, final))

    def test_a_relative_location_resolves_against_the_current_url(self):
        opener = RedirectingOpener({URL_A: Redirect("/moved/a.txt", 308), "https://lists.example/moved/a.txt": A})
        self.assertEqual(fl._download(URL_A, opener)[1], "https://lists.example/moved/a.txt")

    def test_too_many_redirects_are_refused(self):
        opener = RedirectingOpener({URL_A: Redirect(URL_A)})
        with self.assertRaisesRegex(fl.ListError, "too many redirects"):
            fl._download(URL_A, opener)

    def test_an_opener_that_followed_to_http_by_itself_is_caught_by_the_final_url(self):
        class Followed(io.BytesIO):
            def geturl(self):
                return "http://evil.example/a.txt"
        with self.assertRaisesRegex(fl.ListError, "not https"):
            fl._download(URL_A, lambda request: Followed(A))

    def test_a_redirect_without_a_location_is_refused(self):
        class Bare(io.BytesIO):
            status = 302
            headers = {}
        with self.assertRaisesRegex(fl.ListError, "Location"):
            fl._download(URL_A, lambda request: Bare())

    def test_the_default_list_opener_does_not_follow_redirects(self):
        import urllib.request
        handler = fl.NoRedirect()
        req = urllib.request.Request(URL_A)
        self.assertIsNone(handler.redirect_request(req, None, 302, "Found", {}, "http://evil.example/"))


class VerifyDirTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.dir = Path(self._tmp.name)
        (self.dir / "a.txt").write_bytes(A)
        (self.dir / "b.txt").write_bytes(B)

    def test_matching_files_pass(self):
        fl.verify_dir(PINS, self.dir)

    def test_changed_list_fails_closed_and_says_what_to_do(self):
        (self.dir / "a.txt").write_bytes(A + b"||new.example^\n")
        with self.assertRaisesRegex(fl.ListError, "sha256 mismatch.*aerie lists update"):
            fl.verify_dir(PINS, self.dir)

    def test_same_size_different_content_is_caught(self):
        tampered = A.replace(b"ads", b"xyz")
        self.assertEqual(len(tampered), len(A))
        (self.dir / "a.txt").write_bytes(tampered)
        with self.assertRaisesRegex(fl.ListError, "sha256 mismatch"):
            fl.verify_dir(PINS, self.dir)

    def test_missing_and_empty_files_fail(self):
        (self.dir / "a.txt").write_bytes(b"")
        with self.assertRaises(fl.ListError):
            fl.verify_dir(PINS, self.dir)
        (self.dir / "a.txt").unlink()
        with self.assertRaisesRegex(fl.ListError, "missing"):
            fl.verify_dir(PINS, self.dir)

    def test_header_version_must_match_the_lock(self):
        wrong = fl.ListPin("List A", "a.txt", URL_A, PINS[0].sha256, PINS[0].size, "999")
        with self.assertRaisesRegex(fl.ListError, "header version"):
            fl.verify_dir([wrong, PINS[1]], self.dir)

    def test_the_build_never_fetches(self):
        self.assertFalse(hasattr(fl, "fetch"), "the build reads only committed lists")


class LockTest(unittest.TestCase):
    def write(self, doc):
        tmp = tempfile.NamedTemporaryFile("w", suffix=".json", delete=False)
        self.addCleanup(lambda: Path(tmp.name).unlink())
        json.dump(doc, tmp)
        tmp.close()
        return Path(tmp.name)

    def entry(self, **over):
        entry = {"name": "A", "file": "a.txt", "url": URL_A, "sha256": "0" * 64, "size": 10, "version": "1"}
        entry.update(over)
        return entry

    def test_bad_locks_are_rejected(self):
        for over, text in [({"sha256": "xyz"}, "sha256"), ({"url": "http://x/a.txt"}, "https"),
                           ({"file": "../a.txt"}, "unsafe"), ({"size": 0}, "out of range"),
                           ({"sha256": "A" * 64}, "lowercase")]:
            with self.subTest(over=over), self.assertRaisesRegex(fl.ListError, text):
                fl.load_lock(self.write({"schema": 1, "lists": [self.entry(**over)]}))

    def test_empty_and_wrong_schema_are_rejected(self):
        with self.assertRaises(fl.ListError):
            fl.load_lock(self.write({"schema": 1, "lists": []}))
        with self.assertRaises(fl.ListError):
            fl.load_lock(self.write({"schema": 2, "lists": [self.entry()]}))

    def test_duplicate_files_are_rejected(self):
        with self.assertRaisesRegex(fl.ListError, "duplicate"):
            fl.load_lock(self.write({"schema": 1, "lists": [self.entry(), self.entry(name="B")]}))


class UpdateTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.dir = Path(self._tmp.name)
        self.lock = self.dir / "filter_lists.lock.json"
        self.lock.write_text(json.dumps({"schema": 1, "comment": "keep me", "lists": [
            {"name": "List A", "file": "a.txt", "url": URL_A, "sha256": "0" * 64, "size": 5, "version": "1"}]}))
        (self.dir / "a.txt").write_bytes(b"old\n")

    def test_update_writes_the_list_and_rewrites_the_lock(self):
        newer = list_bytes("List A", "101", "||more.example^\n")
        fl.update(self.lock, Opener({URL_A: newer}), lambda m: None)
        doc = json.loads(self.lock.read_text())
        self.assertEqual(doc["comment"], "keep me")
        self.assertEqual(doc["lists"][0]["version"], "101")
        self.assertEqual(doc["lists"][0]["sha256"], hashlib.sha256(newer).hexdigest())
        self.assertEqual((self.dir / "a.txt").read_bytes(), newer)
        fl.verify_dir(fl.load_lock(self.lock), self.dir)  # what was written is what the lock says

    def test_update_refuses_a_redirect_to_http_and_pins_nothing(self):
        before = self.lock.read_text()
        with self.assertRaisesRegex(fl.ListError, "not https"):
            fl.update(self.lock, RedirectingOpener({URL_A: Redirect("http://evil.example/a.txt")}), lambda m: None)
        self.assertEqual(self.lock.read_text(), before)
        self.assertEqual((self.dir / "a.txt").read_bytes(), b"old\n")

    def test_update_records_where_the_bytes_came_from(self):
        final = "https://cdn.example/a.txt"
        newer = list_bytes("List A", "101")
        fl.update(self.lock, RedirectingOpener({URL_A: Redirect(final), final: newer}), lambda m: None)
        self.assertEqual(json.loads(self.lock.read_text())["lists"][0]["final_url"], final)

    def test_update_refuses_a_list_with_the_wrong_title_and_changes_nothing(self):
        before = self.lock.read_text()
        with self.assertRaisesRegex(fl.ListError, "title"):
            fl.update(self.lock, Opener({URL_A: list_bytes("Something else", "9")}), lambda m: None)
        self.assertEqual(self.lock.read_text(), before)
        self.assertEqual((self.dir / "a.txt").read_bytes(), b"old\n")

    def test_update_refuses_a_list_without_a_version_header(self):
        with self.assertRaisesRegex(fl.ListError, "Version"):
            fl.update(self.lock, Opener({URL_A: b"! Title: List A\n||x^\n"}), lambda m: None)

    def test_update_refuses_an_empty_or_failed_download(self):
        with self.assertRaises(fl.ListError):
            fl.update(self.lock, Opener({URL_A: b""}), lambda m: None)
        with self.assertRaisesRegex(fl.ListError, "cannot download"):
            fl.update(self.lock, Opener({}), lambda m: None)
        self.assertEqual((self.dir / "a.txt").read_bytes(), b"old\n")

    def test_one_bad_list_leaves_all_files_alone(self):
        doc = json.loads(self.lock.read_text())
        doc["lists"].append({"name": "List B", "file": "b.txt", "url": URL_B, "sha256": "0" * 64, "size": 5, "version": "1"})
        self.lock.write_text(json.dumps(doc))
        with self.assertRaises(fl.ListError):
            fl.update(self.lock, Opener({URL_A: A, URL_B: b"garbage"}), lambda m: None)
        self.assertEqual((self.dir / "a.txt").read_bytes(), b"old\n")


class RepoLockTest(unittest.TestCase):
    def test_the_committed_lock_loads_and_names_easylist_and_easyprivacy_only(self):
        pins = fl.load_lock(fl.lock_path(REPO))
        self.assertEqual([p.name for p in pins], ["EasyList", "EasyPrivacy"])
        self.assertEqual(sorted(p.file for p in pins), ["easylist.txt", "easyprivacy.txt"])
        for p in pins:
            self.assertTrue(p.url.startswith("https://easylist.to/easylist/"))

    def test_the_committed_list_bytes_match_the_lock(self):
        fl.verify_dir(fl.load_lock(fl.lock_path(REPO)), fl.lock_path(REPO).parent)

    def test_credits_files_are_there_and_match_the_lock(self):
        directory = fl.lock_path(REPO).parent
        readme = (directory / "README.chromium").read_text()
        for field in ("Name:", "License: GPL-3.0", "License File: LICENSE", "Shipped: yes"):
            self.assertIn(field, readme)
        licence = (directory / "LICENSE").read_text()
        self.assertIn("GNU GENERAL PUBLIC LICENSE", licence)
        self.assertIn("Version 3", licence)
        for pin in fl.load_lock(fl.lock_path(REPO)):
            self.assertIn(pin.version, readme, f"README.chromium does not mention {pin.name} {pin.version}")

    def test_the_engine_build_uses_the_same_files_as_the_lock(self):
        gn = (REPO / "overlay/components/aerie_adblock/BUILD.gn").read_text()
        for pin in fl.load_lock(fl.lock_path(REPO)):
            self.assertIn(f"$_lists_dir/{pin.file}", gn)

    def test_compile_engine_checks_hashes_a_second_time(self):
        import subprocess
        import sys
        script = REPO / "overlay/components/aerie_adblock/compile_engine.py"
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            lock = tmp / "lock.json"
            data = tmp / "easylist.txt"
            data.write_bytes(A)
            lock.write_text(json.dumps({"lists": [{"file": "easylist.txt", "sha256": "1" * 64}]}))
            selfcheck = tmp / "selfcheck.json"
            selfcheck.write_text(json.dumps({"block": [], "allow": []}))
            tool = tmp / "tool"
            tool.write_text("#!/bin/sh\nexit 0\n")
            tool.chmod(0o755)
            out = tmp / "out" / "engine.dat"
            proc = subprocess.run(
                [sys.executable, "-I", str(script), "--tool", str(tool), "--out", str(out), "--selfcheck",
                 str(selfcheck), "--lock", str(lock), "--list", str(data)],
                capture_output=True, text=True)
            self.assertNotEqual(proc.returncode, 0)
            self.assertIn("lock says", proc.stderr)
            self.assertFalse(out.exists())

    def test_compile_engine_fails_when_the_tool_writes_nothing(self):
        import subprocess
        import sys
        script = REPO / "overlay/components/aerie_adblock/compile_engine.py"
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            data = tmp / "easylist.txt"
            data.write_bytes(A)
            lock = tmp / "lock.json"
            lock.write_text(json.dumps({"lists": [{"file": "easylist.txt", "sha256": hashlib.sha256(A).hexdigest()}]}))
            selfcheck = tmp / "selfcheck.json"
            selfcheck.write_text(json.dumps({"block": [], "allow": []}))
            tool = tmp / "tool"
            tool.write_text("#!/bin/sh\nexit 0\n")  # exits 0 and writes nothing
            tool.chmod(0o755)
            out = tmp / "out" / "engine.dat"
            proc = subprocess.run(
                [sys.executable, "-I", str(script), "--tool", str(tool), "--out", str(out), "--selfcheck",
                 str(selfcheck), "--lock", str(lock), "--list", str(data)],
                capture_output=True, text=True)
            self.assertNotEqual(proc.returncode, 0)
            self.assertIn("wrote no engine", proc.stderr)

    def test_compile_engine_runs_a_tool_named_relative_to_the_build_dir(self):
        # On Linux the host toolchain is the default one, so GN rebases the tool to a bare
        # "aerie_adblock_compile" in out/Default. That is a path, not a name to look up in PATH.
        import subprocess
        import sys
        script = REPO / "overlay/components/aerie_adblock/compile_engine.py"
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            data = tmp / "easylist.txt"
            data.write_bytes(A)
            lock = tmp / "lock.json"
            lock.write_text(json.dumps({"lists": [{"file": "easylist.txt", "sha256": hashlib.sha256(A).hexdigest()}]}))
            selfcheck = tmp / "selfcheck.json"
            selfcheck.write_text(json.dumps({"block": [], "allow": []}))
            tool = tmp / "aerie_adblock_compile"
            tool.write_text('#!/bin/sh\nwhile [ "$1" != --out ]; do shift; done\necho engine > "$2"\n')
            tool.chmod(0o755)
            proc = subprocess.run(
                [sys.executable, "-I", str(script), "--tool", "aerie_adblock_compile", "--out", "gen/engine.dat",
                 "--selfcheck", str(selfcheck), "--lock", str(lock), "--list", str(data)],
                capture_output=True, text=True, cwd=tmp, env={"PATH": "/usr/bin:/bin"})
            self.assertEqual(proc.returncode, 0, proc.stderr)
            self.assertTrue((tmp / "gen/engine.dat").is_file())

    def test_compile_engine_passes_cosmetic_self_checks_to_the_tool(self):
        import subprocess
        import sys
        script = REPO / "overlay/components/aerie_adblock/compile_engine.py"
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            data = tmp / "easylist.txt"
            data.write_bytes(A)
            lock = tmp / "lock.json"
            lock.write_text(json.dumps({"lists": [{"file": "easylist.txt", "sha256": hashlib.sha256(A).hexdigest()}]}))
            selfcheck = tmp / "selfcheck.json"
            selfcheck.write_text(json.dumps({
                "block": [], "allow": [],
                "hide": [{"url": "https://news.test/", "classes": "ad-slot", "ids": "adbox", "selector": ".ad-slot"}],
                "keep": [{"url": "https://news.test/", "classes": "story", "ids": "", "selector": ".story"}]}))
            args_file = tmp / "args.txt"
            tool = tmp / "aerie_adblock_compile"
            tool.write_text(f'#!/bin/sh\nprintf "%s\\n" "$@" > {args_file}\n'
                            'while [ "$1" != --out ]; do shift; done\necho engine > "$2"\n')
            tool.chmod(0o755)
            proc = subprocess.run(
                [sys.executable, "-I", str(script), "--tool", str(tool), "--out", str(tmp / "gen/engine.dat"),
                 "--selfcheck", str(selfcheck), "--lock", str(lock), "--list", str(data)],
                capture_output=True, text=True)
            self.assertEqual(proc.returncode, 0, proc.stderr)
            args = args_file.read_text().splitlines()
            self.assertIn("--expect-hide", args)
            self.assertIn("https://news.test/|ad-slot|adbox|.ad-slot", args)
            self.assertIn("--expect-keep", args)
            self.assertIn("https://news.test/|story||.story", args)

    def test_selfcheck_json_has_cosmetic_checks_that_the_lists_back(self):
        selfcheck = json.loads((REPO / "overlay/components/aerie_adblock/selfcheck.json").read_text())
        easylist = (REPO / "overlay/third_party/filter_lists/easylist.txt").read_text().splitlines()
        self.assertTrue(selfcheck["hide"] and selfcheck["keep"])
        for item in selfcheck["hide"]:
            self.assertTrue(any(line == "##" + item["selector"] for line in easylist), f"EasyList has no ##{item['selector']}")
        for item in selfcheck["keep"]:
            self.assertFalse(any(line.endswith("##" + item["selector"]) or line.endswith("#@#" + item["selector"])
                             for line in easylist),
                             f"EasyList hides {item['selector']}")


if __name__ == "__main__":
    unittest.main()
