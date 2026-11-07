import hashlib
import tempfile
import textwrap
import unittest
from pathlib import Path
from unittest import mock

from aerie_build import lock, pipeline
from aerie_build.config import BuildPaths
from aerie_build.lock import VerificationError


class FetchSourcesTest(unittest.TestCase):
    """fetch_sources re-checks every download against helium.lock on each build."""

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        tmp = Path(self._tmp.name)
        self.repo = tmp / "repo"
        core = self.repo / "helium-chromium"
        platform = self.repo / "helium-linux"
        core.mkdir(parents=True)
        platform.mkdir(parents=True)
        (core / "downloads.ini").write_text(
            textwrap.dedent(
                """
                [chromium]
                url = https://x/base.tar.zst
                download_filename = base.tar.zst
                output_path = ./
                """
            )
        )
        (platform / "downloads.ini").write_text("")
        (core / "deps.ini").write_text(
            textwrap.dedent(
                """
                [ublock_origin]
                url = https://x/ubo.zip
                download_filename = ubo.zip
                output_path = ./ubo

                [dep]
                url = https://x/dep.zip
                download_filename = dep.zip
                output_path = ./dep
                """
            )
        )
        self.paths = BuildPaths(tmp / "root")
        self.paths.downloads.mkdir(parents=True)
        (self.paths.downloads / "base.tar.zst").write_bytes(b"base")
        (self.paths.downloads / "dep.zip").write_bytes(b"dep")
        pins = lock.Lock(
            helium_version="0.1.2.3",
            chromium_version="1.0.0.0",
            core_tag="t",
            core_commit="c",
            platform_tag="t",
            platform_commit="c",
            downloads={
                "base.tar.zst": hashlib.sha256(b"base").hexdigest(),
                "dep.zip": hashlib.sha256(b"dep").hexdigest(),
            },
        )
        self.ctx = pipeline.Context(
            repo=self.repo,
            paths=self.paths,
            pins=pins,
            cache_size="30G",
            jobs=None,
            log_path=self.paths.logs / "test.log",
            source_mode="helium",
        )
        self.log = pipeline.Logger(self.ctx.log_path)
        self.ran = []
        patcher = mock.patch.object(
            pipeline, "run", side_effect=lambda _log, cmd, *_a, **_k: self.ran.append(cmd)
        )
        patcher.start()
        self.addCleanup(patcher.stop)

    def tearDown(self):
        self.log.close()
        self._tmp.cleanup()

    def mark_unpacked(self):
        self.ctx.tree.mkdir(parents=True)
        (self.ctx.tree / ".downloaded.stamp").touch()

    def unpack_calls(self):
        return [c for c in self.ran if "unpack" in map(str, c)]

    def test_already_unpacked_tree_still_verifies_downloads(self):
        self.mark_unpacked()
        (self.paths.downloads / "base.tar.zst").write_bytes(b"tampered")
        with self.assertRaisesRegex(VerificationError, "base.tar.zst"):
            pipeline.fetch_sources(self.ctx, self.log, {})

    def test_already_unpacked_tree_is_not_unpacked_again(self):
        self.mark_unpacked()
        pipeline.fetch_sources(self.ctx, self.log, {})
        self.assertEqual(self.unpack_calls(), [])
        self.assertTrue((self.ctx.tree / ".downloaded.stamp").exists())

    def test_fresh_tree_is_verified_then_unpacked(self):
        pipeline.fetch_sources(self.ctx, self.log, {})
        self.assertEqual(len(self.unpack_calls()), 2)
        self.assertTrue((self.ctx.tree / ".downloaded.stamp").exists())

    def test_fresh_tree_with_bad_download_is_never_unpacked(self):
        (self.paths.downloads / "dep.zip").write_bytes(b"tampered")
        with self.assertRaises(VerificationError):
            pipeline.fetch_sources(self.ctx, self.log, {})
        self.assertEqual(self.unpack_calls(), [])
        self.assertFalse((self.ctx.tree / ".downloaded.stamp").exists())


if __name__ == "__main__":
    unittest.main()
