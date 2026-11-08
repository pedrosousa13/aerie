"""The masked GPU strings exist twice, in C++ (components/aerie_gpu_info) and in the Bench check. Keep them equal."""

import re
import unittest
from pathlib import Path

from aerie_bench.checks.hardening import gpu

REPO = Path(__file__).resolve().parent.parent
SOURCE = (REPO / "overlay" / "components" / "aerie_gpu_info" / "gpu_info.cc").read_text()
FAMILIES = {"kAmd": "AMD", "kIntel": "Intel", "kNvidia": "NVIDIA", "kMesa": "Mesa", "kOther": "Other"}


def cpp_string(function: str, family: str) -> str:
    body = re.search(function + r"\(GpuFamily family\) \{(.*?)\n\}\n", SOURCE, re.S).group(1)
    return re.search(r"case GpuFamily::" + family + r':\s*return "([^"]*)";', body).group(1)


class MaskedStringsMirrorTest(unittest.TestCase):
    def test_bench_strings_equal_the_cpp_strings(self):
        for cpp_name, name in FAMILIES.items():
            self.assertEqual(gpu.MASKED[name], (cpp_string("MaskedVendor", cpp_name), cpp_string("MaskedRenderer", cpp_name)))

    def test_every_family_is_covered(self):
        self.assertEqual(set(gpu.MASKED), set(FAMILIES.values()))

    def test_no_masked_string_names_aerie_or_is_blank(self):
        for vendor, renderer in gpu.MASKED.values():
            for text in (vendor, renderer):
                self.assertTrue(text.strip())
                self.assertNotIn("aerie", text.lower())
                self.assertFalse(gpu.digits_beyond_version(text), text)

    def test_the_fixture_page_exists(self):
        self.assertTrue((REPO / "tools" / "aerie_bench" / "fixtures" / "hardening" / "gpu.html").exists())

    def test_the_gl_version_in_the_strings_is_the_one_the_check_strips(self):
        for _, renderer in gpu.MASKED.values():
            self.assertIn(gpu.GL_VERSION, renderer)


if __name__ == "__main__":
    unittest.main()
