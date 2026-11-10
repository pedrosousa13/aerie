"""Row padding on every Aerie-owned Settings surface (issue #205).

A row that Aerie adds to Settings must take its inset from the Settings row structure (`cr-row` or
`settings-box`, which read `--cr-section-padding`), never from a bare container or from a pixel value.
Two traps caused the gaps in the Aerie Guard page and the Reduce motion row:

* `.list-item` (settings_shared_lit.css) sets `padding: 0`, and it loads after `.cr-row`, so a
  `cr-row list-item` row has no inset at all.
* `.settings-row` is only a flex group with no padding, so text inside it sits on the card edge.

These tests read the templates, so they run in the PR gate. The run-time proof on a built browser is the
Bench check `row_padding` (tests/test_bench_row_padding.py). Negative controls: each rule is run on a
variant with the defect put back and must report it.
"""

import re
import unittest
from html.parser import HTMLParser
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
SETTINGS = REPO / "overlay/chrome/browser/resources/settings"
GUARD_HTML = SETTINGS / "aerie_guard_page/aerie_guard_page.html.ts"
GUARD_TS = SETTINGS / "aerie_guard_page/aerie_guard_page.ts"
MOTION_HTML = SETTINGS / "appearance_page/aerie_motion_row.html.ts"
HARDENING_PATCH = REPO / "patches/hardening/0004-hardening-settings-page-markup.patch"
ROW_CLASSES = {"cr-row", "settings-box"}
VOID = {"input", "br", "hr", "img", "meta", "link"}


def hardening_markup() -> str:
    """The added lines of hardening_page.html in the generated patch."""
    lines, inside = [], False
    for line in HARDENING_PATCH.read_text(encoding="utf-8").splitlines():
        if line.startswith("+++ "):
            inside = line.endswith("hardening_page.html")
        elif inside and line.startswith("+"):
            lines.append(line[1:])
    return "\n".join(lines)


def clean(template: str) -> str:
    template = re.sub(r'"\$\{[^"]*\}"', '""', template)       # attribute values with an expression
    template = re.sub(r"\$\{[^`}]*\}", "", template)           # plain expressions
    return template


class Rows(HTMLParser):
    def __init__(self):
        super().__init__()
        self.stack: list[set[str]] = []
        self.problems: list[str] = []

    def handle_starttag(self, tag, attrs):
        classes = set(dict(attrs).get("class", "").split())
        row_id = dict(attrs).get("id") or tag
        if classes & ROW_CLASSES and "list-item" in classes:
            self.problems.append(f"{row_id}: list-item zeroes the padding of cr-row/settings-box")
        if classes & ROW_CLASSES and "no-padding" in classes:
            self.problems.append(f"{row_id}: no-padding on a row")
        if "cr-padded-text" in classes and not any(s & ROW_CLASSES for s in self.stack):
            self.problems.append(f"{row_id}: text is not inside a cr-row or settings-box, so it has no inline inset")
        if tag not in VOID:
            self.stack.append(classes)

    def handle_endtag(self, tag):
        if tag not in VOID and self.stack:
            self.stack.pop()


def row_problems(template: str) -> list[str]:
    parser = Rows()
    parser.feed(clean(template))
    return parser.problems


def css_blocks(ts: str) -> list[str]:
    return re.findall(r"css`(.*?)`", ts, flags=re.S)


def pixel_spacing(ts: str) -> list[str]:
    """padding/margin declarations with a pixel value, outside the textarea (a control, not a row)."""
    found = []
    for block in css_blocks(ts):
        for selector, body in re.findall(r"([^{}]+)\{([^}]*)\}", block):
            if selector.strip().startswith("textarea"):
                continue
            for decl in body.split(";"):
                if re.match(r"\s*(padding|margin)[\w-]*\s*:.*\d+px", decl):
                    found.append(f"{selector.strip()}: {decl.strip()}")
    return found


class AerieRowTemplatesTest(unittest.TestCase):
    def test_guard_page_rows_keep_their_inset(self):
        self.assertEqual(row_problems(GUARD_HTML.read_text(encoding="utf-8")), [])

    def test_reduce_motion_row_keeps_its_inset(self):
        self.assertEqual(row_problems(MOTION_HTML.read_text(encoding="utf-8")), [])

    def test_hardening_page_rows_keep_their_inset(self):
        markup = hardening_markup()
        self.assertIn("cr-row", markup)
        self.assertEqual(row_problems(markup), [])

    def test_guard_page_styles_use_no_pixel_spacing(self):
        self.assertEqual(pixel_spacing(GUARD_TS.read_text(encoding="utf-8")), [])

    def test_the_filter_list_and_site_rows_are_plain_cr_rows(self):
        html = GUARD_HTML.read_text(encoding="utf-8")
        self.assertIn('class="cr-row list-row"', html)
        self.assertIn('class="cr-row site-row"', html)


class NegativeControlTest(unittest.TestCase):
    def test_a_list_item_row_is_reported(self):
        bad = '<div class="cr-row list-item"><div class="cr-padded-text">x</div></div>'
        self.assertEqual(len(row_problems(bad)), 1)

    def test_text_in_a_bare_settings_row_is_reported(self):
        bad = '<div class="settings-row" id="r"><div class="flex cr-padded-text">x</div></div>'
        self.assertIn("inline inset", row_problems(bad)[0])

    def test_the_old_motion_row_fails(self):
        old = MOTION_HTML.read_text(encoding="utf-8").replace('class="settings-box first"', 'class="settings-row"')
        self.assertNotEqual(old, MOTION_HTML.read_text(encoding="utf-8"))
        self.assertTrue(row_problems(old))

    def test_the_old_guard_rows_fail(self):
        old = GUARD_HTML.read_text(encoding="utf-8").replace("list-row", "list-item list-row", 1)
        self.assertTrue(row_problems(old))

    def test_pixel_spacing_is_reported(self):
        self.assertTrue(pixel_spacing("css`cr-button.x { margin-inline-end: 16px; }`"))
        self.assertFalse(pixel_spacing("css`cr-button.x { margin-inline-end: var(--cr-section-padding); }`"))


if __name__ == "__main__":
    unittest.main()
