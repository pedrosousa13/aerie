import tempfile
import unittest
from pathlib import Path

from aerie_build import helium_deps

REPO = Path(__file__).resolve().parents[1]
INI = """# comment
[ublock_origin]
version = 1.0
url = https://example/%(version)s.zip
output_path = third_party/ublock

[onboarding]
version = 2
url = https://example/o-%(version)s.tgz
output_path = x/onboarding

[search_engines_data]
version = 3
url = https://example/s-%(version)s.tgz
output_path = x/search
"""


class DepsTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.dir = Path(self._tmp.name)
        self.ini = self.dir / "deps.ini"
        self.ini.write_text(INI)

    def test_dropped_section_is_removed_and_the_rest_is_kept_verbatim(self):
        out = helium_deps.write_filtered(self.ini, self.dir / "out" / "deps.ini")
        text = out.read_text()
        self.assertNotIn("ublock", text)
        self.assertNotIn("onboarding", text)
        self.assertIn("[search_engines_data]\nversion = 3\nurl = https://example/s-%(version)s.tgz", text)
        self.assertEqual(helium_deps.sections(out), ["search_engines_data"])

    def test_a_section_that_left_helium_is_an_error(self):
        self.ini.write_text(INI.replace("[ublock_origin]", "[ublock_origin_renamed]"))
        self.assertTrue(helium_deps.check(self.ini))
        with self.assertRaisesRegex(helium_deps.DepsError, "no longer has the \\[ublock_origin\\]"):
            helium_deps.write_filtered(self.ini, self.dir / "o.ini")

    def test_the_pinned_helium_deps_ini_filters_cleanly_and_has_no_ublock(self):
        ini = REPO / "helium-chromium" / "deps.ini"
        self.assertEqual(helium_deps.check(ini), [])
        out = helium_deps.write_filtered(ini, self.dir / "deps.ini")
        self.assertNotIn("ublock", [s.lower() for s in helium_deps.sections(out)])

    def test_every_dropped_section_has_a_ledger_row(self):
        ledger = (REPO / "docs" / "helium-dependency-ledger.md").read_text()
        for name in helium_deps.DROPPED_SECTIONS:
            self.assertIn(f"deps.ini [{name}]", ledger)


if __name__ == "__main__":
    unittest.main()
