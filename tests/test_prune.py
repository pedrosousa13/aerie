import tempfile
import unittest
from pathlib import Path

from aerie_build import prune
from aerie_build.config import BuildPaths


class PlanPruneTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.root = Path(self._tmp.name)
        self.paths = BuildPaths(self.root)
        for name in (
            "chromium-157-base.tar.zst",  # pinned, keep
            "chromium-157-base.tar.zst.hashes",  # hash file of pinned, keep
            "chromium-157-base.tar.zst.minisig",  # signature of pinned, keep
            "old-tool.tar.xz.minisig",  # signature of unpinned file
            "chromium-150-base.tar.zst",  # stale tarball
            "chromium-150-base.tar.zst.hashes",  # stale hash file
            "chromium-157-linux.tar.zst.partial",  # interrupted download
            "chromium-157-linux.tar.zst.rejected",  # quarantined bad download
        ):
            self._touch(self.paths.downloads / name)
        (self.paths.tree("helium-0.21.1.1") / "out" / "Default").mkdir(parents=True)
        (self.paths.tree("helium-0.20.3.1") / "out").mkdir(parents=True)
        (self.paths.toolchain / "ccache-4.14.2").mkdir(parents=True)
        (self.paths.toolchain / "ccache-4.10.0").mkdir(parents=True)
        (self.paths.toolchain / "venv").mkdir(parents=True)
        self._touch(self.paths.ccache / "ccache.conf")

    def tearDown(self):
        self._tmp.cleanup()

    @staticmethod
    def _touch(path: Path):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(b"x")

    def plan(self):
        return prune.plan_prune(
            self.paths,
            pinned_downloads={"chromium-157-base.tar.zst"},
            current_tree="helium-0.21.1.1",
            current_tools={"ccache-4.14.2", "venv"},
        )

    def test_selects_stale_tarballs_partials_and_old_trees(self):
        names = sorted(p.relative_to(self.root).as_posix() for p in self.plan())
        self.assertEqual(
            names,
            [
                "downloads/chromium-150-base.tar.zst",
                "downloads/chromium-150-base.tar.zst.hashes",
                "downloads/chromium-157-linux.tar.zst.partial",
                "downloads/chromium-157-linux.tar.zst.rejected",
                "downloads/old-tool.tar.xz.minisig",
                "src/helium-0.20.3.1",
                "toolchain/ccache-4.10.0",
            ],
        )

    def test_never_selects_current_tree_pinned_files_or_cache(self):
        plan = set(self.plan())
        self.assertNotIn(self.paths.tree("helium-0.21.1.1"), plan)
        self.assertNotIn(self.paths.downloads / "chromium-157-base.tar.zst", plan)
        self.assertNotIn(self.paths.ccache, plan)
        self.assertNotIn(self.paths.ccache / "ccache.conf", plan)

    def test_dry_run_deletes_nothing(self):
        plan = self.plan()
        prune.execute(plan, dry_run=True, log=lambda _msg: None)
        for path in plan:
            self.assertTrue(path.exists(), path)

    def test_execute_removes_files_and_directories(self):
        plan = self.plan()
        prune.execute(plan, dry_run=False, log=lambda _msg: None)
        for path in plan:
            self.assertFalse(path.exists(), path)
        self.assertTrue(self.paths.tree("helium-0.21.1.1").exists())
        self.assertTrue((self.paths.downloads / "chromium-157-base.tar.zst").exists())

    def test_execute_refuses_paths_outside_root(self):
        outside = Path(tempfile.mkdtemp())
        try:
            with self.assertRaises(ValueError):
                prune.execute(
                    [outside], dry_run=False, log=lambda _m: None, root=self.root
                )
            self.assertTrue(outside.exists())
        finally:
            outside.rmdir()

    def test_missing_directories_yield_empty_plan(self):
        empty = BuildPaths(self.root / "nothing-here")
        self.assertEqual(
            prune.plan_prune(empty, set(), "helium-x", set()),
            [],
        )


if __name__ == "__main__":
    unittest.main()
