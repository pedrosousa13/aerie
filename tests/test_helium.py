import tempfile
import unittest
from pathlib import Path

from aerie_build import helium


class HeliumVersionTest(unittest.TestCase):
    def test_matches_helium_version_scheme(self):
        # Mirrors helium-chromium/utils/helium_version.py:
        # MAJOR.(chromium major - 136).PATCH.PLATFORM
        with tempfile.TemporaryDirectory() as tmp:
            core = Path(tmp) / "core"
            platform = Path(tmp) / "platform"
            core.mkdir()
            platform.mkdir()
            (core / "version.txt").write_text("0\n")
            (core / "chromium_version.txt").write_text("157.0.8103.38\n")
            (core / "revision.txt").write_text("1\n")
            (platform / "revision.txt").write_text("1\n")
            self.assertEqual(helium.helium_version(core, platform), "0.21.1.1")


class GnArgsTest(unittest.TestCase):
    def test_concatenates_flags_and_appends_cpu_and_wrapper(self):
        text = helium.gn_args(
            "is_debug=false\n", "use_sysroot=true", "x64", "/r/toolchain/ccache"
        )
        lines = text.splitlines()
        self.assertEqual(lines[0], "is_debug=false")
        self.assertEqual(lines[1], "use_sysroot=true")
        self.assertIn('target_cpu = "x64"', lines)
        self.assertIn('v8_target_cpu = "x64"', lines)
        self.assertIn('cc_wrapper = "/r/toolchain/ccache"', lines)
        self.assertTrue(text.endswith("\n"))

    def test_no_wrapper_line_without_cache(self):
        self.assertNotIn("cc_wrapper", helium.gn_args("a=1\n", "b=2\n", "x64", None))


if __name__ == "__main__":
    unittest.main()
