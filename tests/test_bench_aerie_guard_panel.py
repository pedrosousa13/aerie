"""The aerie_guard_panel check: its judgement, including the negative control (issue #100)."""

import unittest

from aerie_bench.checks import REGISTRY, aerie_guard_panel as ag

GOOD = {"chromeSend": "undefined", "firstState": None, "secondState": None,
        "state_before": '{"exceptions": []}', "state_after": '{"exceptions": []}', "hosts": ["localhost"]}


class JudgeTest(unittest.TestCase):
    def test_registered(self):
        self.assertIn("aerie_guard_panel", REGISTRY)

    def test_good_run_passes(self):
        self.assertEqual(ag.judge(GOOD), [])

    def test_no_observations_fail(self):
        self.assertTrue(ag.judge(None))

    def test_chrome_send_present_fails(self):
        self.assertTrue(ag.judge({**GOOD, "chromeSend": "function"}))

    def test_handler_that_accepts_a_host_from_the_page_fails(self):
        # Negative control: such a handler answers with a site and writes an exception for it.
        bad = {**GOOD, "firstState": {"host": "localhost"},
               "state_after": '{"exceptions": ["localhost"]}'}
        problems = ag.judge(bad)
        self.assertTrue(any("answered with a site" in p for p in problems))
        self.assertTrue(any("state.json" in p for p in problems))


if __name__ == "__main__":
    unittest.main()
