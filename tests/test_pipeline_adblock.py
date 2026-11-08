"""The Filter Engine's build step after the Aerie layers: filter lists, then vendored crates, fail closed."""

import hashlib
import io
import tempfile
import unittest
import urllib.error
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

from aerie_build import pipeline, rust_crates

from . import test_rust_crates as trc  # shares the in-memory crates and lock builders

A = b"[Adblock Plus 2.0]\n! Version: 100\n! Title: EasyList\n||ads.example^\n"
B = b"[Adblock Plus 1.1]\n! Version: 200\n! Title: EasyPrivacy\n||tracker.example^\n"


class PrepareAdblockTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        root = Path(self._tmp.name)
        self.repo = root / "repo"
        lists_dir = self.repo / "overlay" / "third_party" / "filter_lists"
        lists_dir.mkdir(parents=True)
        (lists_dir / "easylist.txt").write_bytes(A)
        (lists_dir / "easyprivacy.txt").write_bytes(B)
        import json
        (lists_dir / "filter_lists.lock.json").write_text(json.dumps({"schema": 1, "lists": [
            {"name": "EasyList", "file": "easylist.txt", "url": "https://l.example/easylist.txt",
             "sha256": hashlib.sha256(A).hexdigest(), "size": len(A), "version": "100"},
            {"name": "EasyPrivacy", "file": "easyprivacy.txt", "url": "https://l.example/easyprivacy.txt",
             "sha256": hashlib.sha256(B).hexdigest(), "size": len(B), "version": "200"}]}))
        PA = b"# Title: Phishing Hosts Blocklist | test\n# Updated: 2026-01-01T00:00:00Z\nevil.example\n"
        (lists_dir / "phishing_filter_hosts.txt").write_bytes(PA)
        (lists_dir / "protection_lists.lock.json").write_text(json.dumps({"schema": 1, "lists": [
            {"name": "Phishing Hosts Blocklist", "file": "phishing_filter_hosts.txt", "url": "https://l.example/pa.txt",
             "sha256": hashlib.sha256(PA).hexdigest(), "size": len(PA), "version": "2026-01-01T00:00:00Z"}]}))
        (self.repo / "rust").mkdir()
        (self.repo / rust_crates.LOCK_FILE).write_text(trc.make_lock())
        self.tree_helper = trc.Tree(self)
        self.tree = self.tree_helper.root
        # What the overlay layer puts in the tree (see test_overlay.py).
        tree_lists = self.tree / "third_party" / "filter_lists"
        tree_lists.mkdir(parents=True)
        for name in ("easylist.txt", "easyprivacy.txt", "phishing_filter_hosts.txt"):
            (tree_lists / name).write_bytes((lists_dir / name).read_bytes())
        script = self.tree / "tools/crates/run_gnrt.py"
        script.parent.mkdir(parents=True)
        script.write_text("# fake")
        self.requested = []
        self.gnrt_calls = []
        self.ctx = SimpleNamespace(
            repo=self.repo, tree=self.tree, python=Path("py"), opener=self.opener,
            paths=SimpleNamespace(downloads=root / "downloads"),
            pins=SimpleNamespace(chromium_version="157.0.8103.38"))
        self.events = []

    def opener(self, request):
        self.requested.append(request.full_url)
        url = request.full_url
        if "static.crates.io" in url:
            name = url.split("/")[4]
            return io.BytesIO({"adblock": trc.ADBLOCK_CRATE, "thiserror": trc.THISERROR1_CRATE}[name])
        raise urllib.error.URLError("blocked")

    def fake_run(self, log, cmd, env, cwd=None):
        self.gnrt_calls.append(cmd[-1])
        if cmd[-1] == "vendor":
            self.tree_helper.vendor(trc.ENTRIES[0], trc.ADBLOCK_FILES)
            self.tree_helper.vendor(trc.ENTRIES[2], trc.THISERROR1_FILES)
        else:
            gn = self.tree / "third_party/rust/adblock/v0_13"
            gn.mkdir(parents=True, exist_ok=True)
            (gn / "BUILD.gn").write_text("# gn")
            (gn / "README.chromium").write_text(trc.CreditsTest.GOOD)
            lic = self.tree / rust_crates.VENDOR_DIR / "adblock-v0_13" / "LICENSE"
            assert lic.is_file()

    def prepare(self):
        with mock.patch.object(pipeline, "run", self.fake_run):
            pipeline.prepare_adblock(self.ctx, self.events.append, {})

    def test_lists_are_checked_and_crates_vendored(self):
        self.prepare()
        self.assertTrue(any("committed lists verified" in e for e in self.events))
        self.assertEqual(self.gnrt_calls, ["vendor", "gen"])
        self.assertTrue((self.tree / ".aerie-crates.stamp").is_file())

    def test_second_run_vendors_nothing(self):
        self.prepare()
        self.gnrt_calls.clear()
        self.prepare()
        self.assertEqual(self.gnrt_calls, [])

    def test_the_lists_are_never_downloaded(self):
        self.prepare()
        self.assertFalse([u for u in self.requested if "l.example" in u or "easylist" in u], self.requested)

    def test_a_changed_crate_lock_vendors_again(self):
        self.prepare()
        self.gnrt_calls.clear()
        lock_path = self.repo / rust_crates.LOCK_FILE
        lock_path.write_text(lock_path.read_text() + "\n# touched\n")
        self.prepare()
        self.assertEqual(self.gnrt_calls, ["vendor", "gen"])

    def test_a_new_crate_patch_vendors_again(self):
        self.prepare()
        self.gnrt_calls.clear()
        d = self.tree / rust_crates.PATCHES_DIR / "thiserror-v1"
        d.mkdir(parents=True)
        (d / "0001-Add-LICENSE.patch").write_text("patch")
        self.prepare()
        self.assertEqual(self.gnrt_calls, ["vendor", "gen"])

    def test_a_list_in_the_tree_that_differs_from_the_lock_stops_the_build_before_crates(self):
        (self.tree / "third_party/filter_lists/easylist.txt").write_bytes(A + b"||new.example^\n")
        with self.assertRaisesRegex(pipeline.BuildError, "filter lists: .*aerie lists update"):
            self.prepare()
        self.assertEqual(self.gnrt_calls, [])

    def test_a_phishing_list_in_the_tree_that_differs_from_the_lock_stops_the_build_before_crates(self):
        tampered = self.tree / "third_party/filter_lists/phishing_filter_hosts.txt"
        tampered.write_bytes(tampered.read_bytes() + b"google.example\n")
        with self.assertRaisesRegex(pipeline.BuildError, "protection lists: .*aerie lists update"):
            self.prepare()
        self.assertEqual(self.gnrt_calls, [])

    def test_a_cargo_toml_changed_by_a_patch_is_checked_again_not_trusted_by_the_stamp(self):
        self.prepare()
        self.gnrt_calls.clear()
        cargo = self.tree / rust_crates.CARGO_TOML
        cargo.write_text(cargo.read_text() + "\n# patched again\n")
        with self.assertRaisesRegex(pipeline.BuildError, "rust crates: .*stale"):
            self.prepare()

    def test_a_vendored_crate_that_differs_from_crates_io_stops_the_build(self):
        original = self.fake_run

        def tampering(log, cmd, env, cwd=None):
            original(log, cmd, env, cwd)
            if cmd[-1] == "gen":
                (self.tree / rust_crates.VENDOR_DIR / "adblock-v0_13/src/lib.rs").write_bytes(b"// owned")

        with mock.patch.object(pipeline, "run", tampering), \
                self.assertRaisesRegex(pipeline.BuildError, "rust crates: .*do not match crates.io"):
            pipeline.prepare_adblock(self.ctx, self.events.append, {})
        self.assertFalse((self.tree / ".aerie-crates.stamp").exists())


if __name__ == "__main__":
    unittest.main()
