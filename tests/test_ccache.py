import tempfile
import unittest
from pathlib import Path

from aerie_build import ccache


class CcacheConfTest(unittest.TestCase):
    def test_conf_caps_size(self):
        text = ccache.conf_text("30G")
        self.assertIn("max_size = 30G\n", text)

    def test_conf_has_chromium_sloppiness(self):
        self.assertIn("sloppiness = time_macros", ccache.conf_text("30G"))

    def test_write_conf_creates_dir_and_overwrites_size(self):
        with tempfile.TemporaryDirectory() as tmp:
            cache_dir = Path(tmp) / "ccache"
            ccache.write_conf(cache_dir, "30G")
            ccache.write_conf(cache_dir, "12G")
            text = (cache_dir / "ccache.conf").read_text()
        self.assertIn("max_size = 12G", text)
        self.assertNotIn("30G", text)


if __name__ == "__main__":
    unittest.main()
