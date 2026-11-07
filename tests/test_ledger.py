"""The Helium dependency ledger must stay true to the pinned Helium checkout."""

import re
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
LEDGER = REPO / "docs" / "helium-dependency-ledger.md"
CORE = REPO / "helium-chromium"
PLATFORM = REPO / "helium-linux"

_PATCH_PREFIXES = ("helium-chromium/patches/", "helium-linux/patches/")


def table_rows(text: str, heading: str) -> list[list[str]]:
    """Cells of each data row of the first table under the '## heading' section."""
    section = text.split(f"## {heading}", 1)[1].split("\n## ", 1)[0]
    rows = [
        [cell.strip() for cell in line.strip().strip("|").split("|")]
        for line in section.splitlines()
        if line.startswith("|")
    ]
    return rows[2:]  # header and separator


def backticked(cell: str) -> list[str]:
    return re.findall(r"`([^`]+)`", cell)


class LedgerFileTest(unittest.TestCase):
    def setUp(self):
        self.text = LEDGER.read_text(encoding="utf-8")

    def test_ledger_has_both_tables(self):
        # Rows are added over time; only the sections and the baseline must exist.
        self.assertIn("## Aerie patches that depend on a Helium patch", self.text)
        self.assertGreater(len(table_rows(self.text, "What the unmodified build takes from Helium")), 5)

    def test_contributor_docs_require_updating_the_ledger(self):
        contributing = (REPO / "CONTRIBUTING.md").read_text(encoding="utf-8")
        self.assertIn("docs/helium-dependency-ledger.md", contributing)
        self.assertIn("in the\nsame pull request", contributing)

    def test_fallback_plan_links_the_ledger(self):
        plan = (REPO / "docs" / "helium-fallback.md").read_text(encoding="utf-8")
        self.assertIn("helium-dependency-ledger.md", plan)


@unittest.skipUnless((CORE / "patches" / "series").is_file(), "submodules not checked out")
class LedgerMatchesSubmodulesTest(unittest.TestCase):
    def setUp(self):
        self.text = LEDGER.read_text(encoding="utf-8")

    def series(self, root: Path) -> set[str]:
        lines = (root / "patches" / "series").read_text(encoding="utf-8").splitlines()
        return {line.strip() for line in lines if line.strip() and not line.startswith("#")}

    def test_every_listed_helium_patch_is_in_a_series_file(self):
        series = {
            "helium-chromium/patches/": self.series(CORE),
            "helium-linux/patches/": self.series(PLATFORM),
        }
        for row in table_rows(self.text, "Aerie patches that depend on a Helium patch"):
            for path in backticked(row[1]) if len(row) > 1 else []:
                prefix = next((p for p in _PATCH_PREFIXES if path.startswith(p)), None)
                self.assertIsNotNone(prefix, f"{path}: start with helium-chromium/patches/ or helium-linux/patches/")
                self.assertIn(path[len(prefix):], series[prefix], f"{path} is not in the series")

    def test_every_path_in_the_baseline_table_exists(self):
        for row in table_rows(self.text, "What the unmodified build takes from Helium"):
            paths = backticked(row[1])
            self.assertTrue(paths, f"row has no path: {row[0]}")
            for path in paths:
                self.assertTrue((REPO / path).exists(), f"{path} is not in the checkout")


if __name__ == "__main__":
    unittest.main()
