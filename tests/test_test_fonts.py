"""Chromium's test fonts for the C++ unit tests: pinned by DEPS, fetched only for test builds."""

import hashlib
import io
import tarfile
import tempfile
import textwrap
import unittest
import urllib.error
from pathlib import Path
from unittest import mock

from aerie_build import test_fonts

OBJECT = "9c07d19d9c5ee1ff94f717e6fb17e0c8c354e6f9"

DEPS = textwrap.dedent("""
    deps = {
      'src/third_party/swiftshader': Var('swiftshader_git') + '/SwiftShader.git' + '@' + 'abc',

      'src/third_party/test_fonts/test_fonts': {
          'dep_type': 'gcs',
          'condition': 'non_git_source',
          'bucket': 'chromium-fonts',
          'objects': [
              {
                  'object_name': '%(object)s',
                  'sha256sum': '%(sha)s',
                  'size_bytes': %(size)d,
                  'generation': 1775663926405386
              },
          ],
      },

      'src/third_party/text-fragments-polyfill/src':
        Var('chromium_git') + '/external/x.git' + '@' + 'abc',
    }
""")


def make_archive(members=None) -> bytes:
    members = {"Ahem.ttf": b"ahem", "Arimo-Regular.ttf": b"arimo"} if members is None else members
    buffer = io.BytesIO()
    with tarfile.open(fileobj=buffer, mode="w:gz") as tar:
        for name, data in members.items():
            info = tarfile.TarInfo(name)
            info.size = len(data)
            tar.addfile(info, io.BytesIO(data))
    return buffer.getvalue()


def deps_for(archive: bytes, object_name: str = OBJECT) -> str:
    return DEPS % {"object": object_name, "sha": hashlib.sha256(archive).hexdigest(), "size": len(archive)}


class ParseDepsTest(unittest.TestCase):
    def pin(self, text=None):
        return test_fonts.parse_deps(deps_for(make_archive()) if text is None else text)

    def test_reads_object_and_sha256_from_deps(self):
        pin = self.pin()
        self.assertEqual(pin.bucket, "chromium-fonts")
        self.assertEqual(pin.object_name, OBJECT)
        self.assertEqual(pin.sha256, hashlib.sha256(make_archive()).hexdigest())
        self.assertEqual(pin.url, f"https://storage.googleapis.com/chromium-fonts/{OBJECT}")

    def test_a_missing_entry_is_an_error(self):
        with self.assertRaisesRegex(test_fonts.TestFontsError, "no .*test_fonts"):
            self.pin("deps = {'src/other': 'x'}")

    def test_a_non_gcs_entry_is_an_error(self):
        with self.assertRaisesRegex(test_fonts.TestFontsError, "gcs"):
            self.pin(deps_for(make_archive()).replace("'gcs'", "'git'"))

    def test_a_malformed_sha256_is_an_error(self):
        text = deps_for(make_archive())
        text = text.replace(hashlib.sha256(make_archive()).hexdigest(), "ABC")
        with self.assertRaisesRegex(test_fonts.TestFontsError, "sha256"):
            self.pin(text)

    def test_an_unsafe_object_name_is_an_error(self):
        with self.assertRaisesRegex(test_fonts.TestFontsError, "object_name"):
            self.pin(deps_for(make_archive(), object_name="../../evil"))

    def test_an_unsafe_bucket_is_an_error(self):
        with self.assertRaisesRegex(test_fonts.TestFontsError, "bucket"):
            self.pin(deps_for(make_archive()).replace("chromium-fonts", "a/b"))

    def test_two_objects_are_an_error(self):
        text = deps_for(make_archive())
        extra = "{'object_name': '%s', 'sha256sum': '%s', 'size_bytes': 1}," % (OBJECT, "0" * 64)
        text = text.replace("'objects': [", "'objects': [" + extra)
        with self.assertRaisesRegex(test_fonts.TestFontsError, "exactly one"):
            self.pin(text)


class EnsureTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        root = Path(self._tmp.name)
        self.tree = root / "tree"
        self.tree.mkdir()
        self.downloads = root / "downloads"
        self.archive = make_archive()
        (self.tree / "DEPS").write_text(deps_for(self.archive))
        self.requested = []
        self.served = self.archive
        self.events = []

    @property
    def fonts(self) -> Path:
        return self.tree / "third_party" / "test_fonts" / "test_fonts"

    def opener(self, request):
        self.requested.append(request.full_url)
        return io.BytesIO(self.served)

    def ensure(self):
        test_fonts.ensure(self.tree, self.downloads, self.opener, self.events.append)

    def test_downloads_verifies_and_unpacks_into_the_tree(self):
        self.ensure()
        self.assertEqual(self.requested, [f"https://storage.googleapis.com/chromium-fonts/{OBJECT}"])
        self.assertEqual((self.fonts / "Ahem.ttf").read_bytes(), b"ahem")
        self.assertEqual((self.fonts / "Arimo-Regular.ttf").read_bytes(), b"arimo")

    def test_the_archive_is_cached_in_the_downloads_directory(self):
        self.ensure()
        self.assertTrue(any(p.name.endswith(OBJECT + ".tar.gz") for p in self.downloads.iterdir()))

    def test_a_second_run_fetches_nothing(self):
        self.ensure()
        self.ensure()
        self.assertEqual(len(self.requested), 1)

    def test_a_cached_archive_is_not_fetched_again_after_the_tree_is_reset(self):
        self.ensure()
        import shutil
        shutil.rmtree(self.fonts.parent)
        self.ensure()
        self.assertEqual(len(self.requested), 1)
        self.assertTrue((self.fonts / "Ahem.ttf").is_file())

    def test_a_missing_font_in_the_tree_unpacks_again(self):
        self.ensure()
        (self.fonts / "Ahem.ttf").unlink()
        self.ensure()
        self.assertTrue((self.fonts / "Ahem.ttf").is_file())

    def test_a_checksum_mismatch_stops_with_a_clear_error_and_unpacks_nothing(self):
        self.served = make_archive({"Ahem.ttf": b"tampered"})
        with self.assertRaises(test_fonts.TestFontsError) as ctx:
            self.ensure()
        message = str(ctx.exception)
        self.assertIn("sha256 mismatch", message)
        self.assertIn(hashlib.sha256(self.archive).hexdigest(), message)
        self.assertFalse(self.fonts.exists())

    def test_a_rejected_download_is_not_kept_as_a_cache_hit(self):
        self.served = b"junk"
        with self.assertRaises(test_fonts.TestFontsError):
            self.ensure()
        self.served = self.archive
        self.ensure()  # the bad file was moved aside, so this fetches again
        self.assertEqual(len(self.requested), 2)

    def test_a_tampered_cached_archive_is_rejected(self):
        self.downloads.mkdir()
        (self.downloads / f"chromium-test-fonts-{OBJECT}.tar.gz").write_bytes(b"junk")
        with self.assertRaisesRegex(test_fonts.TestFontsError, "sha256 mismatch"):
            self.ensure()
        self.assertEqual(self.requested, [])

    def test_a_non_https_url_is_refused(self):
        with mock.patch.object(test_fonts, "URL_TEMPLATE", "http://storage.googleapis.com/{bucket}/{object}"):
            with self.assertRaisesRegex(Exception, "https"):
                self.ensure()
        self.assertEqual(self.requested, [])

    def test_a_network_failure_is_a_clear_error(self):
        def failing(request):
            raise urllib.error.URLError("offline")
        with self.assertRaisesRegex(Exception, "cannot fetch"):
            test_fonts.ensure(self.tree, self.downloads, failing, self.events.append)

    def test_members_outside_the_font_directory_are_refused(self):
        for name in ("../escape.ttf", "sub/Ahem.ttf", "/abs.ttf"):
            with self.subTest(name=name):
                self.served = make_archive({name: b"x"})
                (self.tree / "DEPS").write_text(deps_for(self.served))
                shutil_target = self.downloads
                if shutil_target.exists():
                    for p in shutil_target.iterdir():
                        p.unlink()
                with self.assertRaisesRegex(test_fonts.TestFontsError, "unsafe"):
                    self.ensure()
                self.assertFalse((self.tree / "escape.ttf").exists())

    def test_an_archive_without_ahem_is_an_error(self):
        self.served = make_archive({"Arimo-Regular.ttf": b"x"})
        (self.tree / "DEPS").write_text(deps_for(self.served))
        with self.assertRaisesRegex(test_fonts.TestFontsError, "Ahem.ttf"):
            self.ensure()


if __name__ == "__main__":
    unittest.main()
