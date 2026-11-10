"""Bench check row_padding (issue #205): judge() against observations.

The rows are hand-written examples of what the page script returns, not a recording of a browser.
"""

import unittest

from aerie_bench import checks
from aerie_bench.checks.row_padding import MEASURE_JS, RowPadding, row_problems


def row(label="r", start=20, end=20, inset=20, token=20):
    return {"index": 0, "label": label, "token": token, "start": start, "end": end, "inset": inset}


def obs(**over):
    base = {"surfaces": {"aerie_guard": {"ready": True, "rows": [row("a"), row("b")]},
                         "appearance": {"ready": True, "rows": [row("aerieMotionRow")]}},
            "control_rows": [row("a", start=0, end=0, inset=0), row("b")]}
    base.update(over)
    return base


class RowPaddingJudgeTest(unittest.TestCase):
    def test_is_registered(self):
        self.assertIs(checks.get("row_padding"), RowPadding)

    def test_passes_when_every_row_has_the_token_padding(self):
        self.assertEqual(RowPadding.judge(obs())[0], "pass")

    def test_fails_and_names_a_row_without_start_padding(self):
        o = obs()
        o["surfaces"]["aerie_guard"]["rows"][1] = row("lists", start=0, inset=0)
        status, detail = RowPadding.judge(o)
        self.assertEqual(status, "fail")
        self.assertIn("lists", detail)

    def test_fails_on_missing_end_padding(self):
        o = obs()
        o["surfaces"]["appearance"]["rows"] = [row("aerieMotionRow", end=0)]
        self.assertEqual(RowPadding.judge(o)[0], "fail")

    def test_fails_when_text_sits_on_the_edge(self):
        self.assertTrue(row_problems([row(inset=2)]))

    def test_fails_when_a_surface_has_no_rows(self):
        o = obs()
        o["surfaces"]["hardening"] = {"ready": False, "rows": []}
        self.assertEqual(RowPadding.judge(o)[0], "fail")

    def test_negative_control_must_be_seen(self):
        self.assertEqual(RowPadding.judge(obs(control_rows=[row("a")]))[0], "fail")

    def test_the_page_script_reads_the_token_not_a_pixel_value(self):
        self.assertIn("--cr-section-padding", MEASURE_JS)


if __name__ == "__main__":
    unittest.main()
