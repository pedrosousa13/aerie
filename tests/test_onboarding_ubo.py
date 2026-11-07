"""The onboarding page asset must not offer a uBlock Origin toggle.

The onboarding page is a Helium asset (deps.ini [onboarding]) that the build unpacks into
components/helium_onboarding before any patch. patches/adblock/0006 removes its uBlock Origin
toggle. This test unpacks the real pinned tarball from the build root's download cache (when it
is there, and after checking its sha256 against helium.lock), applies the patch the way the
build does, and greps the source. It is skipped when the tarball has not been downloaded.
"""

import re
import shutil
import subprocess
import tarfile
import tempfile
import unittest
from pathlib import Path

from aerie_build import lock, patch_check, patches

REPO = Path(__file__).resolve().parents[1]
PATCH = REPO / "patches" / "adblock" / "0006-remove-onboarding-ublock-toggle.patch"
DOWNLOADS = Path("/mnt/linux2/aerie-build/downloads")
UBO = re.compile(r"ublock|\bubo\b|ublock_assets", re.I)


def onboarding_tarball():
    import configparser
    ini = configparser.ConfigParser(defaults={"_chromium_version": "0"}, interpolation=None)
    ini.read(REPO / "helium-chromium" / "deps.ini")
    section = ini["onboarding"]
    name = section["download_filename"].replace("%(version)s", section["version"])
    return DOWNLOADS / name, section["sha256"]


class OnboardingPatchTest(unittest.TestCase):
    def test_the_patch_is_in_the_series_and_removes_the_toggle_pieces(self):
        self.assertIn("adblock/0006-remove-onboarding-ublock-toggle.patch", (REPO / "patches" / "series").read_text())
        text = PATCH.read_text()
        removed = "\n".join(l for l in text.splitlines() if l.startswith("-") and not l.startswith("---"))
        for piece in ("services.ublock_assets", "ublockTitle", "ublockDesc", "IDS_HELIUM_ONBOARDING_SERVICES_UBLOCK_TITLE",
                      "IDS_HELIUM_ONBOARDING_SERVICES_UBLOCK_DESC"):
            self.assertIn(piece, removed)
        # and adds nothing
        self.assertEqual([l for l in text.splitlines() if l.startswith("+") and not l.startswith("+++")], [])

    def test_the_patched_onboarding_source_has_no_ublock(self):
        tarball, sha = onboarding_tarball()
        if not tarball.is_file():
            self.skipTest(f"{tarball} not downloaded")
        lock.verify_file(tarball, sha)
        helium_sha = lock.load_lock(REPO / "helium.lock").downloads.get(tarball.name)
        self.assertEqual(helium_sha, sha, "helium.lock and deps.ini disagree about the onboarding tarball")
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp) / "components" / "helium_onboarding"
            root.mkdir(parents=True)
            with tarfile.open(tarball, "r:gz") as tf:
                members = [m for m in tf.getmembers() if "node_modules" not in m.name.split("/")
                           and ".github" not in m.name.split("/")]
                tf.extractall(root, members=members, filter="data")
            self.assertTrue(UBO.search((root / "src/pages/HeliumServices.svelte").read_text()),
                            "the unpatched asset is expected to have the toggle (the test would prove nothing otherwise)")
            ok = subprocess.run(["patch", "-p1", "--fuzz=0", "-i", str(PATCH), "-d", tmp], capture_output=True, text=True)
            self.assertEqual(ok.returncode, 0, ok.stdout + ok.stderr)
            hits = []
            for path in root.rglob("*"):
                if path.is_file() and path.suffix in (".svelte", ".ts", ".grdp", ".html", ".json", ".css", ".mts", ".in", ".gn"):
                    for number, line in enumerate(path.read_text(errors="replace").splitlines(), 1):
                        if UBO.search(line):
                            hits.append(f"{path.relative_to(root)}:{number}: {line.strip()}")
            self.assertEqual(hits, [])


if __name__ == "__main__":
    unittest.main()
