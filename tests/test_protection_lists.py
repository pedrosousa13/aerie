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


def list_bytes(title="Phishing Army | Community phishing domain blocklist", generated="2026-11-07T17:00:30Z",
               body="evil.example\nphish.example\n"):
    return (f"#\n# Title: {title}\n# Expires: 4 hours\n# Generated: {generated}\n# Entries: 2\n"
            f"# ======\n\n{body}").encode()


DATA = list_bytes()
PIN = fl.ListPin("Phishing Army", "phishing_army.txt", URL, hashlib.sha256(DATA).hexdigest(), len(DATA),
                 "2026-11-07T17:00:30Z")


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
        (self.dir / "phishing_army.txt").write_bytes(DATA)

    def test_matching_file_passes(self):
        pl.verify_dir([PIN], self.dir)

    def test_changed_list_fails_closed_and_names_the_command(self):
        (self.dir / "phishing_army.txt").write_bytes(DATA + b"new.example\n")
        with self.assertRaisesRegex(fl.ListError, "sha256 mismatch.*aerie lists update"):
            pl.verify_dir([PIN], self.dir)

    def test_missing_file_fails(self):
        (self.dir / "phishing_army.txt").unlink()
        with self.assertRaisesRegex(fl.ListError, "missing"):
            pl.verify_dir([PIN], self.dir)

    def test_header_version_must_match_the_lock(self):
        wrong = fl.ListPin(PIN.name, PIN.file, PIN.url, PIN.sha256, PIN.size, "2020-01-01T00:00:00Z")
        with self.assertRaisesRegex(fl.ListError, "Generated"):
            pl.verify_dir([wrong], self.dir)

    def test_a_list_with_no_domains_is_refused_even_if_pinned(self):
        empty = list_bytes(body="")
        pin = fl.ListPin(PIN.name, PIN.file, PIN.url, hashlib.sha256(empty).hexdigest(), len(empty), PIN.version)
        (self.dir / "phishing_army.txt").write_bytes(empty)
        with self.assertRaisesRegex(fl.ListError, "no domains"):
            pl.verify_dir([pin], self.dir)


class UpdateTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.dir = Path(self._tmp.name)
        self.lock = self.dir / "protection_lists.lock.json"
        self.lock.write_text(json.dumps({"schema": 1, "comment": "keep me", "lists": [
            {"name": "Phishing Army", "file": "phishing_army.txt", "url": URL, "sha256": "0" * 64,
             "size": 5, "version": "1"}]}))
        (self.dir / "phishing_army.txt").write_bytes(b"old\n")

    def test_update_writes_the_list_and_rewrites_the_lock(self):
        newer = list_bytes(generated="2026-11-08T01:00:00Z", body="a.example\nb.example\nc.example\n")
        pl.update(self.lock, Opener({URL: newer}), lambda m: None)
        doc = json.loads(self.lock.read_text())
        self.assertEqual(doc["comment"], "keep me")
        self.assertEqual(doc["lists"][0]["version"], "2026-11-08T01:00:00Z")
        self.assertEqual((self.dir / "phishing_army.txt").read_bytes(), newer)
        pl.verify_dir(fl.load_lock(self.lock), self.dir)

    def test_update_refuses_the_wrong_title_an_empty_or_a_failed_download(self):
        before = self.lock.read_text()
        for files, text in [({URL: list_bytes(title="Something else")}, "title"),
                            ({URL: b""}, "empty"),
                            ({URL: list_bytes(body="")}, "no domains"),
                            ({URL: b"<html>410 Gone</html>"}, "title"),
                            ({}, "cannot download")]:
            with self.subTest(text=text), self.assertRaisesRegex(fl.ListError, text):
                pl.update(self.lock, Opener(files), lambda m: None)
            self.assertEqual(self.lock.read_text(), before)
            self.assertEqual((self.dir / "phishing_army.txt").read_bytes(), b"old\n")

    def test_update_refuses_a_list_without_a_generated_header(self):
        data = b"# Title: Phishing Army | x\nevil.example\n"
        with self.assertRaisesRegex(fl.ListError, "Generated"):
            pl.update(self.lock, Opener({URL: data}), lambda m: None)


class RepoLockTest(unittest.TestCase):
    def test_the_committed_lock_names_phishing_army(self):
        pins = fl.load_lock(pl.lock_path(REPO))
        self.assertEqual([p.name for p in pins], ["Phishing Army"])
        self.assertTrue(pins[0].url.startswith("https://phishing.army/"))

    def test_the_committed_list_matches_the_lock_and_holds_domains(self):
        pl.verify_dir(fl.load_lock(pl.lock_path(REPO)), pl.lock_path(REPO).parent)

    def test_the_fixture_probe_domain_is_in_the_list(self):
        # Phishing Army publishes amiprotected.it as a harmless test domain; the
        # engine build self-check relies on it (selfcheck in compile_protection.py).
        text = (pl.lock_path(REPO).parent / "phishing_army.txt").read_text()
        self.assertIn("\namiprotected.it\n", text)

    def test_credits_name_the_list_its_licence_terms_and_version(self):
        readme = (pl.lock_path(REPO).parent / "README.chromium").read_text()
        for pin in fl.load_lock(pl.lock_path(REPO)):
            self.assertIn(pin.version, readme)
        self.assertIn("Phishing Army", readme)
        self.assertIn("https://phishing.army/license/", readme)


if __name__ == "__main__":
    unittest.main()
