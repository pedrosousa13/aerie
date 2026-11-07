"""The cosmetic_filtering check: its decision logic on recorded measurements, and its fixture page.

The measurements have the shape that the check reads from the live page
(`measure_script` in tools/aerie_bench/checks/cosmetic_filtering.py). The first
real run in the build train proves the browser side.
"""

import json
import re
import unittest
from pathlib import Path

from aerie_bench import fixtures
from aerie_bench.checks import REGISTRY, cosmetic_filtering as cf

REPO = Path(__file__).resolve().parents[1]
EXPECTED = cf.load_expected()
EASYLIST = (REPO / "overlay/third_party/filter_lists/easylist.txt").read_text().splitlines()
ENGINE_OK = {"count": 2, "sum": 40}


def shown(selector):
    return {"selector": selector, "found": True, "display": "block", "width": 300, "height": 60, "rects": 1}


def hidden(selector):
    return {"selector": selector, "found": True, "display": "none", "width": 0, "height": 0, "rects": 0}


def good_measurement():
    return ([hidden(h["selector"]) for h in EXPECTED["hidden"]] + [shown(v["selector"]) for v in EXPECTED["visible"]])


def judge(measured, engine=ENGINE_OK, hijacked=False):
    return cf.judge(EXPECTED, measured, engine, hijacked)


class JudgeTest(unittest.TestCase):
    def test_hidden_ads_and_visible_content_pass(self):
        status, detail, data = judge(good_measurement())
        self.assertEqual(status, "pass", detail)
        self.assertIn(f"{len(EXPECTED['hidden'])}/{len(EXPECTED['hidden'])} ad containers hidden", detail)
        self.assertIn(f"{len(EXPECTED['visible'])}/{len(EXPECTED['visible'])} first-party elements visible", detail)
        self.assertEqual(data["engine"], ENGINE_OK)

    def test_an_ad_container_that_is_still_shown_fails_and_is_named(self):
        measured = good_measurement()
        measured[0] = shown(EXPECTED["hidden"][0]["selector"])
        status, detail, _ = judge(measured)
        self.assertEqual(status, "fail")
        self.assertIn("NOT HIDDEN", detail)
        self.assertIn(EXPECTED["hidden"][0]["selector"], detail)

    def test_display_none_without_layout_collapse_is_not_enough(self):
        measured = good_measurement()
        measured[0] = {**hidden(EXPECTED["hidden"][0]["selector"]), "height": 60}
        self.assertEqual(judge(measured)[0], "fail")

    def test_hidden_by_visibility_only_is_not_hidden(self):
        measured = good_measurement()
        measured[0] = {**shown(EXPECTED["hidden"][0]["selector"]), "display": "block", "height": 60}
        self.assertEqual(judge(measured)[0], "fail")

    def test_first_party_content_that_got_hidden_fails(self):
        measured = good_measurement()
        i = len(EXPECTED["hidden"])
        measured[i] = hidden(EXPECTED["visible"][0]["selector"])
        status, detail, _ = judge(measured)
        self.assertEqual(status, "fail")
        self.assertIn("HIDDEN BUT MUST STAY", detail)
        self.assertIn(EXPECTED["visible"][0]["selector"], detail)

    def test_a_missing_element_fails(self):
        measured = good_measurement()
        measured[0] = {"selector": EXPECTED["hidden"][0]["selector"], "found": False}
        status, detail, _ = judge(measured)
        self.assertEqual(status, "fail")
        self.assertIn("not on the page", detail)
        measured = good_measurement()
        measured[-1] = {"selector": EXPECTED["visible"][-1]["selector"], "found": False}
        self.assertEqual(judge(measured)[0], "fail")

    def test_an_unmeasured_selector_fails(self):
        status, detail, _ = judge(good_measurement()[:-1])
        self.assertEqual(status, "fail")
        self.assertIn(EXPECTED["visible"][-1]["selector"], detail)

    def test_hiding_without_the_engine_does_not_count(self):
        for engine in (None, {"count": 0, "sum": 0}, {"count": 3, "sum": 0}):
            status, detail, _ = judge(good_measurement(), engine)
            self.assertEqual(status, "fail", engine)
            self.assertIn("Aerie.Adblock.CosmeticRuleCount", detail)

    def test_a_page_that_hijacks_the_injection_fails(self):
        status, detail, _ = judge(good_measurement(), hijacked=True)
        self.assertEqual(status, "fail")
        self.assertIn(EXPECTED["hijack_flag"], detail)

    def test_nothing_hidden_names_every_ad(self):
        measured = [shown(h["selector"]) for h in EXPECTED["hidden"]] + [shown(v["selector"]) for v in EXPECTED["visible"]]
        status, detail, _ = judge(measured, {"count": 0, "sum": 0})
        self.assertEqual(status, "fail")
        for item in EXPECTED["hidden"]:
            self.assertIn(item["selector"], detail)


class MeasureScriptTest(unittest.TestCase):
    def test_script_embeds_each_selector_as_json(self):
        script = cf.measure_script([".a", 'b"c'])
        self.assertIn('[".a", "b\\"c"]', script)
        self.assertIn("getComputedStyle", script)
        self.assertIn("getBoundingClientRect", script)


class FixtureTest(unittest.TestCase):
    def test_the_check_is_registered(self):
        self.assertIs(REGISTRY["cosmetic_filtering"], cf.CosmeticFiltering)

    def test_every_hidden_selector_is_a_rule_in_the_pinned_easylist(self):
        for item in EXPECTED["hidden"]:
            self.assertIn(item["rule"], EASYLIST, item)
            self.assertIn(item["selector"].split("[")[0], item["rule"])

    def test_no_exception_rule_covers_the_fixture_host(self):
        # A generic '#@#rule' (no domain) would switch a hide off everywhere.
        for item in EXPECTED["hidden"]:
            self.assertNotIn("#@#" + item["rule"][2:], EASYLIST, item)

    def test_visible_selectors_are_not_hidden_by_any_generic_rule(self):
        for item in EXPECTED["visible"]:
            self.assertNotIn("##" + item["selector"], EASYLIST, item)

    def test_the_page_has_every_element_and_no_external_url(self):
        root = fixtures.FIXTURE_DIR / "cosmetic"
        html = (root / "index.html").read_text()
        for item in EXPECTED["hidden"] + EXPECTED["visible"]:
            sel = item["selector"]
            if sel.startswith("#"):
                self.assertIn(f'id="{sel[1:]}"', html, sel)
            else:
                tag, _, cls = sel.partition(".")
                self.assertRegex(html, rf'<{tag or "[a-z]+"} [^>]*class="[^"]*\b{re.escape(cls)}\b', sel)
        for f in root.iterdir():
            self.assertEqual(re.findall(r"(?:https?:)?//[^\s\"')]+", f.read_text()), [], f.name)

    def test_the_page_css_hides_nothing_itself(self):
        css = (fixtures.FIXTURE_DIR / "cosmetic" / "page.css").read_text()
        self.assertNotIn("display: none", css)
        self.assertNotIn("display:none", css)
        self.assertNotIn("visibility", css)

    def test_the_page_tries_to_hijack_the_injection(self):
        html = (fixtures.FIXTURE_DIR / "cosmetic" / "index.html").read_text()
        self.assertIn(EXPECTED["hijack_flag"], html)
        self.assertIn("adoptedStyleSheets", html)

    def test_the_fixture_server_serves_the_page(self):
        with fixtures.FixtureServer() as s:
            import urllib.request
            body = urllib.request.urlopen(f"{s.base_url}{EXPECTED['page']}").read().decode()
        self.assertIn("ad-slot", body)


class HistogramReadTest(unittest.TestCase):
    def test_count_and_sum_from_a_devtools_histogram(self):
        self.assertEqual(cf.histogram_totals({"count": 3, "sum": 12.0, "buckets": []}), {"count": 3, "sum": 12.0})
        self.assertIsNone(cf.histogram_totals(None))


if __name__ == "__main__":
    unittest.main()
