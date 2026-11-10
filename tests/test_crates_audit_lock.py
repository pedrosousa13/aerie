import tomllib
import unittest
from pathlib import Path

from aerie_build import rust_crates

REPO = Path(__file__).resolve().parent.parent

SAMPLE = '''schema = 1
chromium_version = "1.0"

[[package]]
name = "b"
version = "1.0.0"
source = "registry+https://github.com/rust-lang/crates.io-index"
checksum = "aa"
dependencies = [
 "missing",
]

[[package]]
name = "a"
version = "2.0.0"
source = "registry+https://github.com/rust-lang/crates.io-index"
'''


class AuditLockTest(unittest.TestCase):
    def test_keeps_every_package_and_drops_the_edges(self):
        data = tomllib.loads(rust_crates.audit_lock(SAMPLE))
        self.assertEqual(data["version"], 4)
        self.assertEqual([(p["name"], p["version"]) for p in data["package"]], [("a", "2.0.0"), ("b", "1.0.0")])
        self.assertTrue(all("dependencies" not in p for p in data["package"]))
        self.assertEqual(data["package"][1]["checksum"], "aa")
        self.assertNotIn("schema", data)

    def test_the_committed_lock_converts_in_full(self):
        text = (REPO / rust_crates.LOCK_FILE).read_text(encoding="utf-8")
        data = tomllib.loads(rust_crates.audit_lock(text))
        self.assertEqual(len(data["package"]), len(rust_crates.parse_lock(text)))


if __name__ == "__main__":
    unittest.main()
