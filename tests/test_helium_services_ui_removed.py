"""No user-visible Helium services UI is left once the series is applied (issue #94).

Issue #23 removed the Helium services settings. Helium's patches still add three more places that name
them: the "Review Helium services updates" app menu item (with its command and pref nag), the download
bubble's link and summary for disabled extension downloads, and the status line on chrome://components.
Aerie removes them (patches/services/0009), it does not hide them.

The check works on the patch text, the way the layers are applied: for each marker, the lines Helium's
applied patches add and Aerie's patches add, less the lines Aerie's patches remove, must come to nothing.
"""

import re
import unittest
from pathlib import Path

from aerie_build import patch_check, patches

REPO = Path(__file__).resolve().parent.parent

# What each surface is called in the source. A marker that only Helium's patches add must be removed
# again by an Aerie patch.
MARKERS = {
    "app menu item": [
        r"Review Helium services updates",
        r"IDS_HELIUM_SERVICES_SCHEMA_MENU_ITEM",
        r"AddHeliumSchemaItem",
        r"IDC_HELIUM_SERVICES_OPEN",
        r"ShowHeliumServices",
    ],
    "app menu pref nag": [
        r"kHeliumServicesUpdate",
        r"IDS_APP_MENU_BUTTON_HELIUM_SERVICES_UPDATE",
        r"IDS_APPMENU_TOOLTIP_HELIUM_SERVICES_UPDATE",
    ],
    "download bubble link": [
        r"OPEN_HELIUM_SERVICES",
        r"IDS_DOWNLOAD_BUBBLE_INTERRUPTED_SUBPAGE_SUMMARY_HELIUM_SERVICES_DISABLED",
        r"Enable this feature in Helium services settings",
    ],
    "components page status line": [
        r"IDS_COMPONENTS_SVC_STATUS_ERROR_HELIUM_SERVICES",
        r"See Helium services in settings",
    ],
    "removed settings route": [
        r"chrome://settings/privacy/services",
    ],
}

def changed_lines(text: str) -> tuple[list[str], list[str]]:
    """(added, removed) lines of a patch, without the +++ and --- headers."""
    added, removed = [], []
    for line in text.splitlines():
        if line.startswith("+++") or line.startswith("---"):
            continue
        if line.startswith("+"):
            added.append(line[1:])
        elif line.startswith("-"):
            removed.append(line[1:])
    return added, removed


def series_changes() -> dict[str, tuple[list[str], list[str]]]:
    """Per layer label, every added and removed line of the applied patches (Helium's with its drops
    and adapted copies, then Aerie's)."""
    out: dict[str, tuple[list[str], list[str]]] = {}
    for layer in patch_check.layers(REPO):
        added: list[str] = []
        removed: list[str] = []
        for _name, path in patches.layer_entries(layer):
            a, r = changed_lines(path.read_text(encoding="utf-8", errors="replace"))
            added += a
            removed += r
        out[layer.label] = (added, removed)
    return out


def count(lines: list[str], marker: str) -> int:
    return sum(1 for line in lines if re.search(marker, line))


class NothingLeftTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.changes = series_changes()

    def left(self, marker: str) -> int:
        """Lines with the marker that the applied series adds and does not remove again."""
        added = removed = 0
        for label, (a, r) in self.changes.items():
            added += count(a, marker)
            removed += count(r, marker)
        return added - removed

    def test_every_surface_marker_is_removed_again(self):
        for surface, markers in MARKERS.items():
            for marker in markers:
                with self.subTest(surface=surface, marker=marker):
                    self.assertLessEqual(self.left(marker), 0, f"{surface}: {marker} is still added by the series")

    def test_aerie_adds_none_of_them(self):
        aerie_added = self.changes["aerie"][0]
        for surface, markers in MARKERS.items():
            for marker in markers:
                with self.subTest(surface=surface, marker=marker):
                    self.assertEqual(count(aerie_added, marker), 0)


class PatchTest(unittest.TestCase):
    PATCH = REPO / "patches" / "services" / "0009-remove-the-helium-services-nags.patch"

    def test_the_patch_is_in_the_series_after_the_ones_it_edits(self):
        series = [l.strip() for l in (REPO / "patches" / "series").read_text().splitlines()
                  if l.strip() and not l.startswith("#")]
        entry = "services/0009-remove-the-helium-services-nags.patch"
        self.assertIn(entry, series)
        for earlier in ("services/0006-remove-the-helium-services-ui.patch",
                        "services/0007-components-page-says-aerie.patch"):
            self.assertLess(series.index(earlier), series.index(entry))

    def test_the_download_summary_for_disabled_extension_downloads_says_aerie(self):
        grdp = (REPO / "overlay" / "chrome" / "app" / "aerie_services_strings.grdp").read_text(encoding="utf-8")
        self.assertIn("IDS_AERIE_DOWNLOAD_BUBBLE_EXTENSION_DOWNLOADS_OFF", grdp)
        self.assertNotRegex(grdp, r"(?i)helium")

    def test_the_download_summary_has_no_button(self):
        text = self.PATCH.read_text(encoding="utf-8")
        self.assertNotIn("PopulatePrimarySubpageButton", "\n".join(
            l for l in text.splitlines() if l.startswith("+")))


if __name__ == "__main__":
    unittest.main()
