"""The aerie_guard_panel check: its judgements, including the negative controls (issue #100)."""

import unittest

from aerie_bench import ui_devtools
from aerie_bench.checks import REGISTRY, aerie_guard_panel as ag

GOOD = {"chromeSend": "undefined", "firstState": None, "secondState": None,
        "state_before": '{"exceptions": []}', "state_after": '{"exceptions": []}', "hosts": ["localhost"],
        "prefs_before": "{}", "prefs_after": "{}", "canvas_before": "data:abc", "canvas_after": "data:abc"}
NOISE_OFF = '{"profile": {"content_settings": {"exceptions": {"helium_noise": {"http://[*.]localhost,*": {"setting": 2}}}}}}'


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

    def test_the_manager_saving_state_json_for_the_first_time_passes(self):
        # A fresh profile: no file before; the manager writes its own fields during the check.
        first_save = '{"auto_update": true, "exceptions": [], "last_update_check": "1794218866435"}'
        self.assertEqual(ag.judge({**GOOD, "state_before": None, "state_after": first_save}), [])

    def test_an_exception_written_into_a_new_state_json_fails(self):
        problems = ag.judge({**GOOD, "state_before": None, "state_after": '{"exceptions": ["news.test"]}'})
        self.assertTrue(any("state.json" in p for p in problems))


class FingerprintingCallTest(unittest.TestCase):
    """Issue #102: a fingerprinting call through a handler with no captured tab changes no HELIUM_NOISE setting."""

    def test_a_handler_that_takes_the_site_from_the_page_fails(self):
        # Negative control: such a handler writes an exception for a host the page named, and that site loses its
        # noise, so the canvas the fixture page reads changes.
        bad = {**GOOD, "prefs_after": NOISE_OFF, "canvas_after": "data:real"}
        problems = ag.judge(bad)
        self.assertTrue(any("HELIUM_NOISE setting in Preferences" in p for p in problems))
        self.assertTrue(any("never named" in p for p in problems))
        self.assertTrue(any("canvas" in p for p in problems))

    def test_a_lost_noise_exception_is_seen_before_the_preferences_file_is_written(self):
        # The file is written after a delay; the canvas shows the change at once.
        problems = ag.judge({**GOOD, "canvas_after": "data:real"})
        self.assertEqual(len(problems), 1)
        self.assertIn("canvas", problems[0])

    def test_an_exception_that_was_already_there_is_not_a_change(self):
        self.assertEqual(ag.judge({**GOOD, "prefs_before": NOISE_OFF, "prefs_after": NOISE_OFF}), [])

    def test_the_probe_calls_the_fingerprinting_setter(self):
        self.assertIn("setFingerprintingOn(false)", ag.IMPORT_PROBE)

    def test_no_preferences_file_is_no_change(self):
        self.assertEqual(ag.noise_exceptions(None), {})


REOPENED = {**{step: True for step in ag.REOPEN_STEPS}, "exit": {"clean": True, "code": 0}}
# What the train 11 binary did: the second press crashed the browser (SIGSEGV), so the pipe closed.
CRASHED = {"opened": True, "closed": True, "navigated": True, "reopened": False, "closed_again": False,
           "alive": False, "error": "CdpError: browser pipe is closed", "exit": {"clean": False, "code": -11}}


class JudgeReopenTest(unittest.TestCase):
    def test_a_browser_that_opens_the_panel_again_passes(self):
        self.assertEqual(ag.judge_reopen(REOPENED), [])

    def test_no_observations_fail(self):
        self.assertTrue(ag.judge_reopen(None))

    def test_a_browser_that_crashes_on_the_second_open_fails(self):
        # Negative control: the bug this part was written for.
        problems = ag.judge_reopen(CRASHED)
        self.assertTrue(any("did not open it" in p and "pipe is closed" in p for p in problems))
        self.assertTrue(any("signal 11" in p for p in problems))

    def test_each_missing_step_fails_and_names_the_first(self):
        for i, step in enumerate(ag.REOPEN_STEPS):
            obs = {**REOPENED, **{s: False for s in ag.REOPEN_STEPS[i:]}}
            problems = ag.judge_reopen(obs)
            self.assertEqual(problems, [ag.REOPEN_WHAT[step]], step)

    def test_a_crash_at_exit_fails_even_when_every_step_passed(self):
        # The controller also touched the destroyed bubble manager when its tab closed.
        problems = ag.judge_reopen({**REOPENED, "exit": {"clean": False, "code": -11}})
        self.assertEqual(problems, ["the browser was killed by signal 11 (it crashed)"])

    def test_an_unclean_exit_fails(self):
        self.assertTrue(ag.judge_reopen({**REOPENED, "exit": {"clean": False, "code": 0}}))

    def test_the_check_starts_its_own_browser_with_ui_devtools(self):
        # The flag opens a localhost socket: only the second browser has it, never the main one.
        self.assertIn("--enable-ui-devtools=0", ui_devtools.FLAGS)


if __name__ == "__main__":
    unittest.main()
