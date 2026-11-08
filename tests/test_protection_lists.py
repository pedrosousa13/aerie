"""The phishing and malware lists: lock, verification, update. Nothing here touches the network."""

import hashlib
import io
import json
import tempfile
import unittest
import urllib.error
from pathlib import Path

from aerie_build import filter_lists as fl
from aerie_build import protection_lists as pl

REPO = Path(__file__).resolve().parents[1]
URL = "https://phishing.example/list.txt"


def list_bytes(title="Example List | Community phishing domain blocklist", generated="2026-11-07T17:00:30Z",
               body="evil.example\nphish.example\n"):
    return (f"#\n# Title: {title}\n# Expires: 4 hours\n# Generated: {generated}\n# Entries: 2\n"
            f"# ======\n\n{body}").encode()


def plain_bytes(body="evil.example\nphish.example\nlogin.bank-secure.example\n"):
    """A list as Phishing.Database serves it: domains, no header at all."""
    return body.encode()


DATA = list_bytes()
PIN = fl.ListPin("Example List", "example_list.txt", URL, hashlib.sha256(DATA).hexdigest(), len(DATA),
                 "2026-11-07T17:00:30Z")
PLAIN = plain_bytes()
PLAIN_PIN = fl.ListPin("Phishing.Database", "plain.txt", URL, hashlib.sha256(PLAIN).hexdigest(), len(PLAIN),
                       "retrieved 2026-11-08")


class Opener:
    def __init__(self, files):
        self.files = files

    def __call__(self, request):
        if request.full_url not in self.files:
            raise urllib.error.URLError("blocked")
        return io.BytesIO(self.files[request.full_url])


class VerifyTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.dir = Path(self._tmp.name)
        (self.dir / "example_list.txt").write_bytes(DATA)

    def test_matching_file_passes(self):
        pl.verify_dir([PIN], self.dir)

    def test_changed_list_fails_closed_and_names_the_command(self):
        (self.dir / "example_list.txt").write_bytes(DATA + b"new.example\n")
        with self.assertRaisesRegex(fl.ListError, "sha256 mismatch.*aerie lists update"):
            pl.verify_dir([PIN], self.dir)

    def test_missing_file_fails(self):
        (self.dir / "example_list.txt").unlink()
        with self.assertRaisesRegex(fl.ListError, "missing"):
            pl.verify_dir([PIN], self.dir)

    def test_header_version_must_match_the_lock(self):
        wrong = fl.ListPin(PIN.name, PIN.file, PIN.url, PIN.sha256, PIN.size, "2020-01-01T00:00:00Z")
        with self.assertRaisesRegex(fl.ListError, "Generated"):
            pl.verify_dir([wrong], self.dir)

    def test_a_list_with_no_domains_is_refused_even_if_pinned(self):
        empty = list_bytes(body="")
        pin = fl.ListPin(PIN.name, PIN.file, PIN.url, hashlib.sha256(empty).hexdigest(), len(empty), PIN.version)
        (self.dir / "example_list.txt").write_bytes(empty)
        with self.assertRaisesRegex(fl.ListError, "no domains"):
            pl.verify_dir([pin], self.dir)


class HeaderlessVerifyTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.dir = Path(self._tmp.name)
        (self.dir / "plain.txt").write_bytes(PLAIN)

    def test_a_list_without_a_header_passes_when_the_hash_matches(self):
        pl.verify_dir([PLAIN_PIN], self.dir)

    def test_a_changed_headerless_list_fails_closed(self):
        (self.dir / "plain.txt").write_bytes(PLAIN + b"new.example\n")
        with self.assertRaisesRegex(fl.ListError, "sha256 mismatch.*aerie lists update"):
            pl.verify_dir([PLAIN_PIN], self.dir)


class UpdateTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.dir = Path(self._tmp.name)
        self.lock = self.dir / "protection_lists.lock.json"
        self.lock.write_text(json.dumps({"schema": 1, "comment": "keep me", "lists": [
            {"name": "Example List", "file": "example_list.txt", "url": URL, "sha256": "0" * 64,
             "size": 5, "version": "1"}]}))
        (self.dir / "example_list.txt").write_bytes(b"old\n")

    def test_update_writes_the_list_and_rewrites_the_lock(self):
        newer = list_bytes(generated="2026-11-08T01:00:00Z", body="a.example\nb.example\nc.example\n")
        pl.update(self.lock, Opener({URL: newer}), lambda m: None)
        doc = json.loads(self.lock.read_text())
        self.assertEqual(doc["comment"], "keep me")
        self.assertEqual(doc["lists"][0]["version"], "2026-11-08T01:00:00Z")
        self.assertEqual((self.dir / "example_list.txt").read_bytes(), newer)
        pl.verify_dir(fl.load_lock(self.lock), self.dir)

    def test_update_refuses_the_wrong_title_an_empty_or_a_failed_download(self):
        before = self.lock.read_text()
        for files, text in [({URL: list_bytes(title="Something else")}, "title"),
                            ({URL: b""}, "empty"),
                            ({URL: list_bytes(body="")}, "no domains"),
                            ({URL: b"<html>410 Gone</html>"}, "plain domain list"),
                            ({}, "cannot download")]:
            with self.subTest(text=text), self.assertRaisesRegex(fl.ListError, text):
                pl.update(self.lock, Opener(files), lambda m: None)
            self.assertEqual(self.lock.read_text(), before)
            self.assertEqual((self.dir / "example_list.txt").read_bytes(), b"old\n")

    def test_update_refuses_a_list_without_a_generated_header(self):
        data = b"# Title: Example List | x\nevil.example\n"
        with self.assertRaisesRegex(fl.ListError, "Generated"):
            pl.update(self.lock, Opener({URL: data}), lambda m: None)


class HeaderlessUpdateTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.dir = Path(self._tmp.name)
        self.lock = self.dir / "protection_lists.lock.json"
        self.lock.write_text(json.dumps({"schema": 1, "lists": [
            {"name": "Phishing.Database", "file": "plain.txt", "url": URL, "sha256": "0" * 64,
             "size": 5, "version": "retrieved 2000-01-01"}]}))
        (self.dir / "plain.txt").write_bytes(b"old\n")

    def update(self, data):
        return pl.update(self.lock, Opener({URL: data}), lambda m: None, today=lambda: "2026-11-08")

    def test_update_pins_a_headerless_list_with_the_retrieval_date_as_version(self):
        self.update(PLAIN)
        doc = json.loads(self.lock.read_text())
        self.assertEqual(doc["lists"][0]["version"], "retrieved 2026-11-08")
        self.assertEqual((self.dir / "plain.txt").read_bytes(), PLAIN)
        pl.verify_dir(fl.load_lock(self.lock), self.dir)

    def test_update_refuses_an_error_page_in_place_of_a_headerless_list(self):
        before = self.lock.read_text()
        for data in (b"<html><body><h1>503 Service Unavailable</h1>\nNo server is available.\n</body></html>\n",
                     b"<html>410 Gone</html>", b"", b"# only a comment\n"):
            with self.subTest(data=data[:20]), self.assertRaises(fl.ListError):
                self.update(data)
            self.assertEqual(self.lock.read_text(), before)
            self.assertEqual((self.dir / "plain.txt").read_bytes(), b"old\n")

    def test_update_tolerates_a_few_junk_lines_in_a_big_list(self):
        body = "".join(f"d{i}.example\n" for i in range(2000)) + "%20junk\n40.1.1.1?rid=x\n"
        self.update(plain_bytes(body))


class RepoLockTest(unittest.TestCase):
    FILE = "phishing_database_active.txt"

    def test_the_committed_lock_names_phishing_database(self):
        pins = fl.load_lock(pl.lock_path(REPO))
        self.assertEqual([p.name for p in pins], ["Phishing.Database"])
        self.assertEqual(pins[0].file, self.FILE)
        self.assertEqual(
            pins[0].url,
            "https://raw.githubusercontent.com/Phishing-Database/Phishing.Database/master/"
            "phishing-domains-ACTIVE.txt")

    def test_the_committed_list_matches_the_lock_and_holds_domains(self):
        pl.verify_dir(fl.load_lock(pl.lock_path(REPO)), pl.lock_path(REPO).parent)

    def test_the_self_check_hosts_are_in_the_pinned_list(self):
        # The build self-check (compile_protection.py) needs the listed hosts to be in the list.
        lines = set((pl.lock_path(REPO).parent / self.FILE).read_text().split("\n"))
        checks = json.loads((REPO / "overlay/components/aerie_adblock/protection_selfcheck.json").read_text())
        for host in checks["listed"]:
            root = host
            while root not in lines and "." in root:
                root = root.split(".", 1)[1]
            self.assertIn(root, lines, host)

    def test_credits_name_the_list_its_licence_and_version(self):
        readme = (pl.lock_path(REPO).parent / "README.chromium").read_text()
        for pin in fl.load_lock(pl.lock_path(REPO)):
            self.assertIn(pin.version, readme)
        self.assertIn("Phishing.Database", readme)
        self.assertIn("License: MIT", readme)
        self.assertIn("LICENSE.phishing_database", readme)

    def test_the_licence_file_is_the_mit_text_of_the_publisher(self):
        text = (pl.lock_path(REPO).parent / "LICENSE.phishing_database").read_text()
        self.assertIn("MIT License", text)
        self.assertIn("Permission is hereby granted, free of charge", text)
        self.assertIn("https://github.com/Phishing-Database/Phishing.Database", text)

    def test_the_docs_quote_the_licence_and_link_its_source(self):
        doc = (REPO / "docs" / "phishing-protection.md").read_text()
        self.assertIn("Permission is hereby granted, free of charge", doc)
        self.assertIn("https://github.com/Phishing-Database/Phishing.Database/blob/master/LICENSE", doc)

    def test_the_replaced_list_is_gone_from_the_repo(self):
        old = "phishing" + "[ _.]?" + "army"
        import re
        import subprocess
        out = subprocess.run(["git", "grep", "-n", "-i", "-I", "-E", old, "--", "."], cwd=REPO,
                             capture_output=True, text=True).stdout
        self.assertEqual(out, "")
        self.assertFalse(list(pl.lock_path(REPO).parent.glob("*" + "army" + "*")))
        self.assertIsNone(re.search(old, (pl.lock_path(REPO)).read_text(), re.I))


if __name__ == "__main__":
    unittest.main()
