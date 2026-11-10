"""Chromium's chrome/test/data/webui files, which Helium prunes: pinned by content, fetched only for UI test builds."""

import io
import tarfile
import tempfile
import unittest
import urllib.error
from pathlib import Path
from unittest import mock

from aerie_build import lock, test_webui_data

CHROMIUM = "157.0.8103.38"
FILES = {"BUILD.gn": b"# from the archive", "tsconfig_base.json": b"{}", "about_sys/a.ts": b"export {};"}


def make_archive(files=None, mtime=0, extra=()) -> bytes:
    """A gzip tar like the one gitiles serves: plain files, directories, a modification time that changes."""
    files = FILES if files is None else files
    buffer = io.BytesIO()
    with tarfile.open(fileobj=buffer, mode="w:gz") as tar:
        info = tarfile.TarInfo("about_sys")
        info.type = tarfile.DIRTYPE
        info.mtime = mtime
        tar.addfile(info)
        for name, data in files.items():
            info = tarfile.TarInfo(name)
            info.size = len(data)
            info.mtime = mtime
            tar.addfile(info, io.BytesIO(data))
        for info in extra:
            tar.addfile(info)
    return buffer.getvalue()


def write(path: Path, data: bytes) -> Path:
    path.write_bytes(data)
    return path


class ContentDigestTest(unittest.TestCase):
    def digest(self, data):
        with tempfile.TemporaryDirectory() as tmp:
            return test_webui_data.content_digest(write(Path(tmp) / "a.tar.gz", data))

    def test_the_digest_does_not_depend_on_modification_times(self):
        # gitiles stamps every member with the time of the request, so the archive bytes change on each download.
        self.assertNotEqual(make_archive(mtime=1), make_archive(mtime=2))
        self.assertEqual(self.digest(make_archive(mtime=1)), self.digest(make_archive(mtime=2)))

    def test_the_digest_changes_with_a_file_content(self):
        changed = dict(FILES, **{"about_sys/a.ts": b"export const x = 1;"})
        self.assertNotEqual(self.digest(make_archive()), self.digest(make_archive(changed)))

    def test_the_digest_changes_with_a_file_name(self):
        renamed = {("b.ts" if k == "about_sys/a.ts" else k): v for k, v in FILES.items()}
        self.assertNotEqual(self.digest(make_archive()), self.digest(make_archive(renamed)))

    def test_a_link_member_is_refused(self):
        link = tarfile.TarInfo("evil")
        link.type = tarfile.SYMTYPE
        link.linkname = "/etc/passwd"
        with self.assertRaisesRegex(test_webui_data.TestDataError, "unsafe member"):
            self.digest(make_archive(extra=[link]))

    def test_an_escaping_path_is_refused(self):
        for name in ("../x.ts", "/abs.ts", "a/../../x.ts"):
            with self.subTest(name=name):
                with self.assertRaisesRegex(test_webui_data.TestDataError, "unsafe member"):
                    self.digest(make_archive(dict(FILES, **{name: b"x"})))

    def test_an_archive_without_the_marker_file_is_refused(self):
        with self.assertRaisesRegex(test_webui_data.TestDataError, "tsconfig_base.json"):
            self.digest(make_archive({"BUILD.gn": b"x"}))


class EnsureTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        root = Path(self._tmp.name)
        self.tree = root / "tree"
        self.webui = self.tree / "chrome" / "test" / "data" / "webui"
        self.webui.mkdir(parents=True)
        (self.webui / "BUILD.gn").write_text("# helium's own")
        self.downloads = root / "downloads"
        self.archive = make_archive()
        self.pin = self.make_pin(self.archive)
        self.requested = []
        self.served = self.archive
        self.events = []

    def make_pin(self, archive):
        with tempfile.TemporaryDirectory() as tmp:
            digest = test_webui_data.content_digest(write(Path(tmp) / "a.tar.gz", archive))
        return {"chromium_version": CHROMIUM, "url": test_webui_data.url_for(CHROMIUM), "sha256": digest}

    def opener(self, request):
        self.requested.append(request.full_url)
        return io.BytesIO(self.served)

    def ensure(self, pin=None, chromium=CHROMIUM):
        test_webui_data.ensure(self.tree, self.downloads, self.pin if pin is None else pin, chromium, self.opener, self.events.append)

    def test_downloads_verifies_and_unpacks_into_the_tree(self):
        self.ensure()
        self.assertEqual(self.requested, [self.pin["url"]])
        self.assertEqual((self.webui / "tsconfig_base.json").read_bytes(), b"{}")
        self.assertEqual((self.webui / "about_sys" / "a.ts").read_bytes(), b"export {};")

    def test_the_pdf_dataset_unpacks_into_its_own_directory_with_its_own_stamp_and_archive(self):
        archive = make_archive(files={"basic_plugin_test.ts": b"export {};"})
        with tempfile.TemporaryDirectory() as tmp:
            digest = test_webui_data.content_digest(write(Path(tmp) / "a.tar.gz", archive), "pdf")
        pin = {"chromium_version": CHROMIUM, "url": test_webui_data.url_for(CHROMIUM, "pdf"), "sha256": digest}
        self.served = archive
        test_webui_data.ensure(self.tree, self.downloads, pin, CHROMIUM, self.opener, self.events.append, name="pdf")
        self.assertEqual((self.tree / "chrome/test/data/pdf/basic_plugin_test.ts").read_bytes(), b"export {};")
        self.assertTrue((self.tree / ".aerie-test-data-pdf.stamp").is_file())
        self.assertEqual([p.name for p in self.downloads.iterdir()], [f"chromium-test-data-pdf-{CHROMIUM}.tar.gz"])
        self.assertTrue(self.requested[0].endswith("/chrome/test/data/pdf.tar.gz"))

    def test_the_url_is_an_https_gitiles_archive_of_the_pinned_tag(self):
        self.assertEqual(
            test_webui_data.url_for(CHROMIUM),
            "https://chromium.googlesource.com/chromium/src/+archive/refs/tags/157.0.8103.38/chrome/test/data/webui.tar.gz")

    def test_a_file_the_tree_already_has_is_kept(self):
        self.ensure()
        self.assertEqual((self.webui / "BUILD.gn").read_text(), "# helium's own")

    def test_the_archive_is_kept_in_the_downloads_directory(self):
        self.ensure()
        self.assertEqual([p.name for p in self.downloads.iterdir()], [f"chromium-test-data-webui-{CHROMIUM}.tar.gz"])

    def test_a_second_run_fetches_nothing(self):
        self.ensure()
        self.ensure()
        self.assertEqual(len(self.requested), 1)

    def test_a_cached_archive_with_other_timestamps_is_accepted(self):
        self.downloads.mkdir()
        write(self.downloads / f"chromium-test-data-webui-{CHROMIUM}.tar.gz", make_archive(mtime=99))
        self.ensure()
        self.assertEqual(self.requested, [])
        self.assertTrue((self.webui / "tsconfig_base.json").is_file())

    def test_a_digest_mismatch_unpacks_nothing_and_renames_the_download(self):
        self.served = make_archive(dict(FILES, **{"about_sys/a.ts": b"tampered"}))
        with self.assertRaisesRegex(test_webui_data.TestDataError, "sha256 mismatch"):
            self.ensure()
        self.assertFalse((self.webui / "tsconfig_base.json").exists())
        self.assertEqual([p.name for p in self.downloads.iterdir()],
                         [f"chromium-test-data-webui-{CHROMIUM}.tar.gz.rejected"])

    def test_a_pin_for_another_chromium_version_is_refused_before_any_download(self):
        with self.assertRaisesRegex(test_webui_data.TestDataError, "158.0.1.1"):
            self.ensure(chromium="158.0.1.1")
        self.assertEqual(self.requested, [])

    def test_a_missing_pin_is_refused(self):
        with self.assertRaisesRegex(test_webui_data.TestDataError, "test_data.webui"):
            self.ensure(pin={})

    def test_a_non_https_url_is_refused(self):
        with self.assertRaisesRegex(test_webui_data.TestDataError, "https"):
            self.ensure(pin=dict(self.pin, url=self.pin["url"].replace("https://", "http://")))
        self.assertEqual(self.requested, [])

    def test_a_network_failure_names_the_url(self):
        def failing(request):
            raise urllib.error.URLError("no route")
        with self.assertRaisesRegex(Exception, "googlesource"):
            test_webui_data.ensure(self.tree, self.downloads, self.pin, CHROMIUM, failing, self.events.append)


class RepoPinTest(unittest.TestCase):
    def test_helium_lock_pins_the_data_for_its_own_chromium_version(self):
        pins = lock.load_lock(Path(__file__).resolve().parent.parent / "helium.lock")
        pin = pins.test_data["webui"]
        self.assertEqual(pin["chromium_version"], pins.chromium_version)
        self.assertEqual(pin["url"], test_webui_data.url_for(pins.chromium_version))
        self.assertRegex(pin["sha256"], r"^[0-9a-f]{64}$")

    def test_helium_lock_pins_every_dataset_the_fetcher_unpacks(self):
        pins = lock.load_lock(Path(__file__).resolve().parent.parent / "helium.lock")
        for name in test_webui_data.DATASETS:
            pin = pins.test_data[name]
            self.assertEqual(pin["chromium_version"], pins.chromium_version)
            self.assertEqual(pin["url"], test_webui_data.url_for(pins.chromium_version, name))
        self.assertTrue(pins.test_data["pdf"]["url"].endswith("/chrome/test/data/pdf.tar.gz"))
