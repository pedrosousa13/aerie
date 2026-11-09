"""The online page parser of adblock_score and the online run, against FAKE saved pages.

The pages in tests/data/adblock_online/ are hand-written from the site's source (each file says so at its
top). They are not recordings of the live site.
"""

import unittest
from pathlib import Path

from aerie_bench import adblock_online as ao
from aerie_bench.cdp import CdpError

DATA = Path(__file__).resolve().parent / "data" / "adblock_online"


def page(name):
    return (DATA / name).read_text(encoding="utf-8")


class ParseTest(unittest.TestCase):
    def test_a_finished_page_gives_score_checks_and_categories(self):
        got = ao.parse_page(page("fake_finished.html"))
        self.assertTrue(got["finished"])
        self.assertEqual((got["total"], got["blocked"], got["not_blocked"]), (8, 6, 2))
        self.assertEqual(got["percent"], 75.0)
        self.assertEqual(got["checks"], {"cosmetic_static": True, "cosmetic_dynamic": False,
                                         "script_ads": True, "script_pagead": True})
        self.assertEqual(got["categories"]["Ads"], {
            "vendors": 2, "hosts": 3, "vendors_with_failure": ["Vendor Two"]})
        self.assertEqual(got["categories"]["Analytics"], {"vendors": 1, "hosts": 1, "vendors_with_failure": []})

    def test_a_running_page_is_not_finished_and_has_no_numbers(self):
        got = ao.parse_page(page("fake_running.html"))
        self.assertFalse(got["finished"])
        self.assertIsNone(got["total"])
        self.assertIsNone(got["percent"])
        self.assertIsNone(got["checks"]["cosmetic_static"])

    def test_a_page_without_the_result_card_is_not_finished(self):
        got = ao.parse_page("<html><body><p>Cloudflare says no</p></body></html>")
        self.assertFalse(got["finished"])
        self.assertEqual(got["categories"], {})

    def test_numbers_that_do_not_add_up_are_not_trusted(self):
        html = page("fake_finished.html").replace("2 not blocked", "5 not blocked")
        got = ao.parse_page(html)
        self.assertFalse(got["finished"])
        self.assertIn("do not add up", got["problem"])

    def test_unbalanced_markup_does_not_raise(self):
        self.assertFalse(ao.parse_page("<div><span>x</div></span><svg/>")["finished"])


class FakeSession:
    """Just enough of Session: navigate_url, wait_for, evaluate."""
    def __init__(self, html_by_call, load_error=None, raise_on=None):
        self.html_by_call = list(html_by_call)
        self.load_error = load_error
        self.raise_on = raise_on
        self.urls = []

    def navigate_url(self, url, wait=None):
        self.urls.append(url)
        if self.raise_on == "navigate":
            raise CdpError("boom")
        return {"url": url, "error": self.load_error}

    def evaluate(self, expression, **kw):
        if self.raise_on == "evaluate":
            raise CdpError("gone")
        return self.html_by_call.pop(0) if len(self.html_by_call) > 1 else self.html_by_call[0]


class ReadLiveTest(unittest.TestCase):
    def read(self, session, wait=2.0):
        return ao.read_live(session, wait_seconds=wait, poll_seconds=0.0)

    def test_a_finished_run_is_reported_with_its_score(self):
        s = FakeSession([page("fake_running.html"), page("fake_finished.html")])
        got = self.read(s)
        self.assertEqual(got["state"], "finished")
        self.assertEqual(got["blocked"], 6)
        self.assertEqual(s.urls, [ao.LIVE_URL])

    def test_a_page_that_never_finishes_is_reported_as_such(self):
        got = self.read(FakeSession([page("fake_running.html")]), wait=0.05)
        self.assertEqual(got["state"], "timeout")
        self.assertIn("did not finish", got["detail"])

    def test_a_page_that_fails_to_load_is_reported_not_raised(self):
        got = self.read(FakeSession([""], load_error="net::ERR_NAME_NOT_RESOLVED"))
        self.assertEqual(got["state"], "load_failed")
        self.assertIn("ERR_NAME_NOT_RESOLVED", got["detail"])

    def test_a_driver_error_is_reported_not_raised(self):
        for where in ("navigate", "evaluate"):
            with self.subTest(where=where):
                got = self.read(FakeSession([""], raise_on=where))
                self.assertEqual(got["state"], "error")

    def test_the_live_address_is_https(self):
        self.assertTrue(ao.LIVE_URL.startswith("https://adblock.turtlecute.org"))


if __name__ == "__main__":
    unittest.main()
