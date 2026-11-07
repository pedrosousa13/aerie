import json
import unittest

from aerie_bench import report, scenario
from aerie_bench.scenario import ScenarioError


class ScenarioParseTest(unittest.TestCase):
    def test_default(self):
        sc = scenario.Scenario()
        self.assertEqual(sc.checks, ("smoke",))

    def test_empty_object_means_smoke(self):
        self.assertEqual(scenario.parse("{}").checks, ("smoke",))

    def test_steps_only_scenario_has_no_default_checks(self):
        sc = scenario.parse(json.dumps({"steps": [{"navigate": "/index.html"}]}))
        self.assertEqual(sc.checks, ())

    def test_full_scenario(self):
        sc = scenario.parse(json.dumps({"name": "n", "checks": ["smoke"], "steps": [
            {"navigate": "/index.html"}, {"click": "#go"},
            {"evaluate": "1+1", "expect": "2"}, {"screenshot": "a.png"}, {"wait_ms": 5}]}))
        self.assertEqual([s.action for s in sc.steps], ["navigate", "click", "evaluate", "screenshot", "wait_ms"])
        self.assertEqual(sc.steps[2].expect, "2")

    def test_rejections(self):
        bad = [
            "not json", "[]", '{"bogus":1}', '{"checks":"smoke"}', '{"steps":{}}',
            '{"steps":[{"navigate":"/a","click":"#b"}]}',
            '{"steps":[{}]}', '{"steps":[{"explode":1}]}',
            '{"steps":[{"navigate":"http://example.com/"}]}',   # external URL
            '{"steps":[{"navigate":"//example.com/"}]}',
            '{"steps":[{"screenshot":"../x.png"}]}', '{"steps":[{"screenshot":"x.jpg"}]}',
            '{"steps":[{"wait_ms":-1}]}', '{"steps":[{"wait_ms":99999}]}', '{"steps":[{"wait_ms":true}]}',
            '{"steps":[{"click":"#a","expect":"x"}]}', '{"steps":[{"evaluate":"x","expect":3}]}',
        ]
        for text in bad:
            with self.subTest(text=text), self.assertRaises(ScenarioError):
                scenario.parse(text)

    def test_load_missing_file(self):
        with self.assertRaises(ScenarioError):
            scenario.load("/nonexistent/s.json")


RUN = {
    "scenario": "default", "binary": {"path": "/b/chrome", "version": "Chromium 1"},
    "started": "2026-11-07T12:00:00+00:00", "duration_s": 2.5, "headless": True, "no_sandbox": False,
    "checks": [{"name": "smoke", "status": "pass", "detail": "ok", "duration_s": 1.0, "data": {}},
               {"name": "browser_requests", "status": "fail", "detail": "nothing", "duration_s": 2.0, "data": {}}],
    "network": {"netlog_requests": 3, "fixture_requests": [{}], "external_requests": [
        {"method": "GET", "url": "https://x.invalid/", "net_error": -105}]},
    "exit": {"clean": True, "code": 0}, "profile": {"deleted": True}, "status": "fail",
    "files": ["results.json", "summary.txt"],
}


class ReportTest(unittest.TestCase):
    def test_json_is_versioned_and_round_trips(self):
        data = json.loads(report.to_json(RUN))
        self.assertEqual(data["schema"], 1)
        self.assertEqual(data["checks"][0]["name"], "smoke")
        self.assertEqual(list(data)[0], "schema")

    def test_summary_mentions_everything_a_reader_needs(self):
        text = report.summary(RUN)
        for needle in ("/b/chrome", "Chromium 1", "PASS   smoke", "FAIL   browser_requests",
                       "1 external (refused)", "https://x.invalid/", "clean (code 0)",
                       "profile   deleted", "result    FAIL"):
            self.assertIn(needle, text)


if __name__ == "__main__":
    unittest.main()
