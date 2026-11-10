"""Bench check row_padding (issue #205): judge() against observations.

The rows are hand-written examples of what the page script returns, not a recording of a browser.
"""

import unittest

from aerie_bench import checks
from aerie_bench.checks.row_padding import MEASURE_JS, RowPadding, row_problems


def row(label="r", start=20, end=20, inset=20, token=20):
    return {"index": 0, "label": label, "token": token, "start": start, "end": end, "inset": inset}


def obs(**over):
    base = {"neighbours": [row("n", start=20, end=20)],
            "surfaces": {"aerie_guard": {"ready": True, "rows": [row("a"), row("b")]},
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

    def test_fails_when_rows_differ_from_the_built_in_neighbours(self):
        o = obs(neighbours=[row("n", start=16, end=16, token=16)])
        status, detail = RowPadding.judge(o)
        self.assertEqual(status, "fail")
        self.assertIn("neighbour", detail)

    def test_fails_when_no_neighbour_rows_could_be_read(self):
        self.assertEqual(RowPadding.judge(obs(neighbours=[]))[0], "fail")

    def test_fails_when_a_required_row_is_missing(self):
        o = obs()
        o["surfaces"]["aerie_guard"]["missing"] = [".custom-rules-row"]
        status, detail = RowPadding.judge(o)
        self.assertEqual(status, "fail")
        self.assertIn("custom-rules-row", detail)

    def test_the_guard_surface_requires_the_own_rules_row(self):
        from aerie_bench.checks.row_padding import SURFACES
        self.assertIn(".custom-rules-row", SURFACES["aerie_guard"]["required"])

    def test_a_card_with_no_padding_fails_and_a_padded_card_passes(self):
        card = dict(row("panel"), card=True)
        self.assertEqual(row_problems([dict(card, start=16, end=16, inset=16, token=0)]), [])
        self.assertTrue(row_problems([dict(card, start=0, end=16, inset=0, token=0)]))

    def test_the_setup_page_is_measured_on_a_step_that_has_option_cards(self):
        from aerie_bench.checks.row_padding import SURFACES
        self.assertTrue(SURFACES["setup"]["card"])
        self.assertEqual(SURFACES["setup"]["setup_step"], "search")
        self.assertNotIn("guard_panel", SURFACES)

    def test_helium_subpage_title_row_is_not_measured_and_animations_are_settled(self):
        from aerie_bench.checks.row_padding import SETTLE_JS, SURFACES
        self.assertEqual(SURFACES["hardening"]["exclude"], "#headerLine")
        self.assertIn("getAnimations", SETTLE_JS)

    def test_a_continuation_row_does_not_set_the_neighbour_padding(self):
        from aerie_bench.checks.row_padding import neighbour_padding
        # Hand-written rows modelled on what train 20 read on the Appearance page: one 'cr-row continuation' with start 0.
        rows = [row("cr-row", start=20), row("cr-row continuation", start=0), row("cr-row", start=20)]
        self.assertEqual(neighbour_padding(rows), 20)

    def test_the_page_script_reads_the_token_not_a_pixel_value(self):
        self.assertIn("--cr-section-padding", MEASURE_JS)


if __name__ == "__main__":
    unittest.main()
