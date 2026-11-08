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
HEADER = ADBLOCK / "common/cosmetic_limits.h"
SOURCE = ADBLOCK / "browser/cosmetic_scripts.cc"


def rust_const(name):
    match = re.search(rf"pub const {name}: usize = ([0-9_ *]+);", RUST_LIB.read_text())
    assert match, f"{name} not found in {RUST_LIB}"
    result = 1
    for factor in match.group(1).replace("_", "").split("*"):
        result *= int(factor)
    return result


def cpp_const(name):
    match = re.search(rf"inline constexpr size_t {name} = ([0-9' *]+);", HEADER.read_text())
    assert match, f"{name} not found in {HEADER}"
    # A plain number or a product of plain numbers, like 1024 * 1024.
    result = 1
    for factor in match.group(1).replace("'", "").split("*"):
        result *= int(factor)
    return result


class CosmeticLimitsTest(unittest.TestCase):
    def test_max_page_tokens_agree(self):
        self.assertEqual(cpp_const("kMaxPageTokens"), rust_const("MAX_PAGE_TOKENS"))

    def test_max_token_bytes_agree(self):
        self.assertEqual(cpp_const("kMaxTokenBytes"), rust_const("MAX_TOKEN_BYTES"))

    def test_max_sheet_bytes_agree_with_the_core_css_cap(self):
        """The renderer refuses a sheet bigger than the core can produce."""
        self.assertEqual(cpp_const("kMaxSheetBytes"), rust_const("MAX_COSMETIC_CSS_BYTES"))

    def test_the_limits_have_one_definition(self):
        """The browser's scripts and the renderer agent include the one header; neither declares its own."""
        for path in ADBLOCK.rglob("*.h"):
            if path == HEADER:
                continue
            text = path.read_text()
            for name in ("kMaxPageTokens", "kMaxTokenBytes", "kMaxScannedElements", "kMaxSheetBytes"):
                self.assertNotRegex(text, rf"constexpr size_t {name}\b", f"{name} declared again in {path.name}")

    def test_the_source_has_no_second_copy_of_a_limit(self):
        """The script and the size check are built from the constants."""
        code = "\n".join(
            line for line in SOURCE.read_text().splitlines() if not line.lstrip().startswith("//")
        )
        for number in (rust_const("MAX_PAGE_TOKENS"), cpp_const("kMaxScannedElements"), rust_const("MAX_TOKEN_BYTES") + 1):
            self.assertNotRegex(code, rf"(?<![0-9]){number}(?![0-9])", f"literal {number} in {SOURCE.name}")


if __name__ == "__main__":
    unittest.main()
