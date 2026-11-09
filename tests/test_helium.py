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
            "is_debug=false\nsafe_browsing_mode=0\n", "use_sysroot=true", "x64", "/r/toolchain/ccache"
        )
        lines = text.splitlines()
        self.assertEqual(lines[0], "is_debug=false")
        self.assertEqual(lines[2], "use_sysroot=true")
        self.assertIn('target_cpu = "x64"', lines)
        self.assertIn('v8_target_cpu = "x64"', lines)
        self.assertIn('cc_wrapper = "/r/toolchain/ccache"', lines)
        self.assertTrue(text.endswith("\n"))

    def test_no_wrapper_line_without_cache(self):
        self.assertNotIn("cc_wrapper", helium.gn_args("safe_browsing_mode=0\n", "b=2\n", "x64", None))


class SafeBrowsingGateTest(unittest.TestCase):
    """Google Safe Browsing must stay compiled out: the local list is the only phishing check."""

    def test_mode_zero_passes(self):
        self.assertEqual(helium.safe_browsing_problems("a=1\nsafe_browsing_mode=0\n"), [])
        self.assertEqual(helium.safe_browsing_problems("safe_browsing_mode = 0\n"), [])

    def test_a_non_zero_mode_fails(self):
        for mode in ("1", "2"):
            problems = helium.safe_browsing_problems(f"safe_browsing_mode={mode}\n")
            self.assertEqual(len(problems), 1)
            self.assertIn("safe_browsing_mode", problems[0])

    def test_the_last_assignment_wins(self):
        self.assertEqual(helium.safe_browsing_problems("safe_browsing_mode=1\nsafe_browsing_mode=0\n"), [])
        self.assertTrue(helium.safe_browsing_problems("safe_browsing_mode=0\nsafe_browsing_mode=2\n"))

    def test_unset_fails_because_chromium_defaults_to_on(self):
        self.assertTrue(helium.safe_browsing_problems("is_debug=false\n"))

    def test_a_comment_does_not_count(self):
        self.assertTrue(helium.safe_browsing_problems("# safe_browsing_mode=0\n"))

    def test_gn_args_refuses_to_assemble_args_with_safe_browsing_on(self):
        with self.assertRaises(helium.SafeBrowsingEnabled):
            helium.gn_args("safe_browsing_mode=0\n", "safe_browsing_mode=1\n", "x64", None)
        helium.gn_args("safe_browsing_mode=0\n", "b=2\n", "x64", None)

    def test_the_pinned_helium_flags_pass(self):
        root = Path(__file__).resolve().parent.parent
        flags = (root / "helium-chromium" / "flags.gn").read_text()
        platform = (root / "helium-linux" / "flags.linux.gn").read_text()
        self.assertEqual(helium.safe_browsing_problems(flags + platform), [])


if __name__ == "__main__":
    unittest.main()
