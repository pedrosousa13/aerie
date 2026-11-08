"""The token limits of cosmetic filtering have one value in Rust and in C++.

The Rust core caps what it reads from a page (`MAX_PAGE_TOKENS`,
`MAX_TOKEN_BYTES`). The browser's collect script and its size check for the
reply must use the same numbers, or the page is read in a way the core ignores
(or the reply is refused). No compiler is needed: the values are read from the
sources.
"""

import re
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
ADBLOCK = REPO / "overlay/components/aerie_adblock"
RUST_LIB = ADBLOCK / "rust/core/src/lib.rs"
HEADER = ADBLOCK / "browser/cosmetic_scripts.h"
SOURCE = ADBLOCK / "browser/cosmetic_scripts.cc"


def rust_const(name):
    match = re.search(rf"pub const {name}: usize = ([0-9_]+);", RUST_LIB.read_text())
    assert match, f"{name} not found in {RUST_LIB}"
    return int(match.group(1).replace("_", ""))


def cpp_const(name):
    match = re.search(rf"inline constexpr size_t {name} = ([0-9']+);", HEADER.read_text())
    assert match, f"{name} not found in {HEADER}"
    return int(match.group(1).replace("'", ""))


class CosmeticLimitsTest(unittest.TestCase):
    def test_max_page_tokens_agree(self):
        self.assertEqual(cpp_const("kMaxPageTokens"), rust_const("MAX_PAGE_TOKENS"))

    def test_max_token_bytes_agree(self):
        self.assertEqual(cpp_const("kMaxTokenBytes"), rust_const("MAX_TOKEN_BYTES"))

    def test_the_source_has_no_second_copy_of_a_limit(self):
        """The script and the size check are built from the constants."""
        code = "\n".join(
            line for line in SOURCE.read_text().splitlines() if not line.lstrip().startswith("//")
        )
        for number in (rust_const("MAX_PAGE_TOKENS"), cpp_const("kMaxScannedElements"), rust_const("MAX_TOKEN_BYTES") + 1):
            self.assertNotRegex(code, rf"(?<![0-9]){number}(?![0-9])", f"literal {number} in {SOURCE.name}")


if __name__ == "__main__":
    unittest.main()
