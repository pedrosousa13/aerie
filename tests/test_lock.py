import hashlib
import tempfile
import textwrap
import unittest
from pathlib import Path

from aerie_build import lock
from aerie_build.lock import VerificationError

from .test_minisign import CCACHE_FILE, CCACHE_PUB, CCACHE_SIG

LOCK_TEXT = textwrap.dedent(
    """
    [helium]
    version = "0.21.1.1"
    chromium_version = "157.0.8103.38"
    core_tag = "0.21.1"
    core_commit = "ecb36f09c4a4140e4cf4e67e77a3150bf3f3e7eb"
    platform_tag = "0.21.1.1"
    platform_commit = "4da5fdc8f90175592f62b6a3f62e90a65e171789"

    [downloads]
    "a.tar.zst" = "{a}"
    "b.zip" = "{b}"

    [tools.ccache]
    version = "4.14.2"
    url = "https://example.invalid/ccache.tar.xz"
    minisign_key_file = "keys/ccache.pub"
    """
)


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


class LockTestCase(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self._tmp.name)
        self.cache = self.dir / "downloads"
        self.cache.mkdir()
        (self.cache / "a.tar.zst").write_bytes(b"alpha")
        (self.cache / "b.zip").write_bytes(b"beta")
        self.lock_path = self.dir / "helium.lock"
        self.lock_path.write_text(LOCK_TEXT.format(a=sha(b"alpha"), b=sha(b"beta")))
        (self.dir / "keys").mkdir()
        (self.dir / "keys" / "ccache.pub").write_text(
            "untrusted comment: minisign public key 7C55E705DB25EF17\n" + CCACHE_PUB + "\n"
        )

    def tearDown(self):
        self._tmp.cleanup()


class LoadLockTest(LockTestCase):
    def test_reads_versions_downloads_and_tools(self):
        pins = lock.load_lock(self.lock_path)
        self.assertEqual(pins.helium_version, "0.21.1.1")
        self.assertEqual(pins.chromium_version, "157.0.8103.38")
        self.assertEqual(pins.core_commit, "ecb36f09c4a4140e4cf4e67e77a3150bf3f3e7eb")
        self.assertEqual(set(pins.downloads), {"a.tar.zst", "b.zip"})
        self.assertEqual(pins.tools["ccache"]["version"], "4.14.2")
        self.assertEqual(pins.tools["ccache"]["minisign_key"].strip().splitlines()[-1], CCACHE_PUB)

    def test_tool_without_publisher_signature_key_is_rejected(self):
        text = LOCK_TEXT.format(a=sha(b"alpha"), b=sha(b"beta"))
        self.lock_path.write_text(text.replace('minisign_key_file = "keys/ccache.pub"\n', ""))
        with self.assertRaisesRegex(VerificationError, "minisign"):
            lock.load_lock(self.lock_path)

    def test_tool_with_invalid_key_is_rejected(self):
        (self.dir / "keys" / "ccache.pub").write_text("garbage\n")
        with self.assertRaises(VerificationError):
            lock.load_lock(self.lock_path)

    def test_rejects_malformed_digest(self):
        self.lock_path.write_text(LOCK_TEXT.format(a="nothex", b=sha(b"beta")))
        with self.assertRaises(VerificationError):
            lock.load_lock(self.lock_path)


class VerifyDownloadsTest(LockTestCase):
    def test_passes_when_all_files_match(self):
        pins = lock.load_lock(self.lock_path)
        lock.verify_downloads(pins, self.cache, ["a.tar.zst", "b.zip"])

    def test_fails_closed_on_mismatch(self):
        (self.cache / "a.tar.zst").write_bytes(b"tampered")
        pins = lock.load_lock(self.lock_path)
        with self.assertRaisesRegex(VerificationError, "a.tar.zst"):
            lock.verify_downloads(pins, self.cache, ["a.tar.zst"])

    def test_fails_closed_when_file_has_no_pin(self):
        (self.cache / "new.tar.gz").write_bytes(b"x")
        pins = lock.load_lock(self.lock_path)
        with self.assertRaisesRegex(VerificationError, "no pinned"):
            lock.verify_downloads(pins, self.cache, ["new.tar.gz"])

    def test_fails_closed_when_file_missing(self):
        (self.cache / "b.zip").unlink()
        pins = lock.load_lock(self.lock_path)
        with self.assertRaisesRegex(VerificationError, "missing"):
            lock.verify_downloads(pins, self.cache, ["b.zip"])

    def test_fails_closed_when_nothing_to_verify(self):
        pins = lock.load_lock(self.lock_path)
        with self.assertRaises(VerificationError):
            lock.verify_downloads(pins, self.cache, [])

    def test_mismatch_quarantines_the_bad_file(self):
        (self.cache / "a.tar.zst").write_bytes(b"tampered")
        pins = lock.load_lock(self.lock_path)
        with self.assertRaises(VerificationError):
            lock.verify_downloads(pins, self.cache, ["a.tar.zst"])
        self.assertFalse((self.cache / "a.tar.zst").exists())


class VerifyFileTest(LockTestCase):
    def test_accepts_matching_digest_case_insensitively(self):
        lock.verify_file(self.cache / "a.tar.zst", sha(b"alpha").upper())

    def test_rejects_mismatch(self):
        with self.assertRaises(VerificationError):
            lock.verify_file(self.cache / "a.tar.zst", sha(b"other"))


class VerifyToolArchiveTest(LockTestCase):
    def test_bad_signature_fails_closed_and_quarantines(self):
        pins = lock.load_lock(self.lock_path)
        archive = self.cache / CCACHE_FILE
        archive.write_bytes(b"tampered tarball")
        with self.assertRaisesRegex(VerificationError, "ccache"):
            lock.verify_tool_archive("ccache", pins.tools["ccache"], archive, CCACHE_SIG)
        self.assertFalse(archive.exists())
        self.assertTrue(archive.with_name(archive.name + ".rejected").exists())

    def test_missing_signature_fails_closed(self):
        pins = lock.load_lock(self.lock_path)
        archive = self.cache / CCACHE_FILE
        archive.write_bytes(b"x")
        with self.assertRaises(VerificationError):
            lock.verify_tool_archive("ccache", pins.tools["ccache"], archive, "")


class RequiredDownloadsTest(unittest.TestCase):
    def test_expands_chromium_version_and_section_vars(self):
        with tempfile.TemporaryDirectory() as tmp:
            core = Path(tmp) / "core.ini"
            core.write_text(
                textwrap.dedent(
                    """
                    [chromium]
                    url = https://x/chromium-%(_chromium_version)s-base.tar.zst
                    download_filename = chromium-%(_chromium_version)s-base.tar.zst
                    hash_url = chromium|chromium-%(_chromium_version)s-base.tar.zst.hashes|https://h
                    output_path = ./
                    """
                )
            )
            deps = Path(tmp) / "deps.ini"
            deps.write_text(
                textwrap.dedent(
                    """
                    [ublock]
                    version = 1.2.3
                    url = https://x/u-%(version)s.zip
                    download_filename = ublock-%(version)s.zip
                    sha256 = abc
                    output_path = third_party/ublock
                    """
                )
            )
            names = lock.required_downloads([core, deps], "157.0.1.2")
        self.assertEqual(names, ["chromium-157.0.1.2-base.tar.zst", "ublock-1.2.3.zip"])


if __name__ == "__main__":
    unittest.main()
