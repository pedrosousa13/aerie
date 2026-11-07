"""Checks that helium.lock agrees with the pinned submodules.

These run against the real checkout, so they catch a Helium bump that
forgot to update helium.lock (or the reverse).
"""

import unittest
from pathlib import Path

from aerie_build import helium, lock

REPO = Path(__file__).resolve().parent.parent
CORE = REPO / "helium-chromium"
PLATFORM = REPO / "helium-linux"


@unittest.skipUnless((CORE / "downloads.ini").is_file(), "submodules not checked out")
class RepoLockTest(unittest.TestCase):
    def setUp(self):
        self.pins = lock.load_lock(REPO / "helium.lock")

    def test_lock_pins_every_download_the_build_needs(self):
        inis = [CORE / "downloads.ini", PLATFORM / "downloads.ini", CORE / "deps.ini"]
        needed = set(lock.required_downloads(inis, self.pins.chromium_version))
        self.assertEqual(needed - set(self.pins.downloads), set())

    def test_lock_versions_match_submodules(self):
        self.assertEqual(
            (CORE / "chromium_version.txt").read_text().strip(), self.pins.chromium_version
        )
        self.assertEqual(helium.helium_version(CORE, PLATFORM), self.pins.helium_version)


if __name__ == "__main__":
    unittest.main()
