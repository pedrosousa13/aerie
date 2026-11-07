"""The phishing_protection Bench check: its judge on good and bad observations, its fixtures, the hop server.

The observations are built by hand in the shape ``observe_listed_browser`` returns. The first real run in
the build train writes the real ones (``phishing_protection`` result data); compare them with ``good()``.
"""

import copy
import unittest
import urllib.parse
import urllib.error
import urllib.request
from pathlib import Path

from aerie_bench import fixtures
from aerie_bench.checks import REGISTRY, phishing_protection as pp

REPO = Path(__file__).resolve().parents[1]
PORT = 4242


def url(host, path=pp.LANDING):
    return f"http://{host}:{PORT}{path}"


def interstitial(**over):
    state = {"href": "chrome-error://chromewebdata/", "title": pp.INTERSTITIAL_TITLE, "interstitial": True,
             "primary_text": "Go back", "proceed_link": True, "details_closed": True,
             "body_class": "safe-browsing", "page_script_ran": False, "body_background": "rgb(255, 255, 255)"}
    state.update(over)
    return state


def good():
    main_url, sub_url = url("phish.localhost"), url("www.phish.localhost")
    return {
        "control": {"title": pp.LANDING_TITLE, "landing_hits": 1},
        "listed": {
            "clean": {"title": pp.LANDING_TITLE, "landing_hits": 1},
            "main": {"url": main_url, "state": interstitial(), "landing_hits": 0,
                     "frames": [{"url": "chrome-error://chromewebdata/", "unreachable": main_url}]},
            "subdomain": {"url": sub_url, "state": interstitial(), "landing_hits": 0,
                          "frames": [{"url": "chrome-error://chromewebdata/", "unreachable": sub_url}]},
            "go_back": {"interstitial": True, "clicked": True, "back_on_clean": True, "landing_hits": 0},
            "continue": {"first": {"interstitial": True, "details_closed": True}, "details_opened": True,
                         "clicked_link": True, "loaded": True, "after": {"title": pp.LANDING_TITLE, "href": "x"},
                         "same_tab_again": {"title": pp.LANDING_TITLE, "interstitial": False},
                         "new_tab_title": pp.INTERSTITIAL_TITLE, "landing_hits": 2},
            "subframe": {"frame_url": url("sub.localhost"), "listed_hits": 0, "host_title": "Fixture frame host",
                         "frames": [{"url": url("clean.localhost", "/protection/frame.html"), "unreachable": None},
                                    {"url": "chrome-error://chromewebdata/", "unreachable": url("sub.localhost")}]},
            "redirect_main": {"state": interstitial(), "target": url("chain.localhost"), "hop_hits": 1,
                              "listed_hits": 0,
                              "frames": [{"url": "chrome-error://chromewebdata/",
                                          "unreachable": url("chain.localhost")}]},
            "redirect_subframe": {"target": url("chainsub.localhost"), "hop_hits": 2, "listed_hits": 0,
                                  "frames": [{"url": "x", "unreachable": None},
                                             {"url": "chrome-error://chromewebdata/",
                                              "unreachable": url("chainsub.localhost")}]},
            "histograms": {"list_load": {"true": 1, "false": 0}, "blocked": {"true": 9, "false": 6},
                           "proceeded": {"true": 1, "false": 1}},
        },
    }


def verdict(obs):
    status, detail, _ = pp.judge(obs)
    return status, detail


class JudgeTest(unittest.TestCase):
    def mutate(self, fn):
        obs = copy.deepcopy(good())
        fn(obs)
        return verdict(obs)

    def assertFails(self, fn, text):
        status, detail = self.mutate(fn)
        self.assertEqual(status, "fail", detail)
        self.assertIn(text, detail)

    def test_a_correct_browser_passes(self):
        status, detail = verdict(good())
        self.assertEqual(status, "pass", detail)
        self.assertIn("Go back primary", detail)

    def test_the_negative_control_must_load_the_listed_name_without_the_list(self):
        self.assertFails(lambda o: o["control"].update(title=pp.INTERSTITIAL_TITLE), "negative control failed")
        self.assertFails(lambda o: o["control"].update(landing_hits=0), "negative control failed")

    def test_no_interstitial_on_the_main_frame_fails(self):
        def loaded(o):
            o["listed"]["main"]["state"] = {"title": pp.LANDING_TITLE, "interstitial": False}
        self.assertFails(loaded, "main frame: no interstitial")

    def test_the_blocked_site_must_not_run_script_or_style_the_page(self):
        self.assertFails(lambda o: o["listed"]["main"]["state"].update(page_script_ran=True), "script ran")
        self.assertFails(lambda o: o["listed"]["main"]["state"].update(body_background="rgb(0, 255, 0)"),
                         "style is applied")

    def test_the_request_must_never_reach_the_server(self):
        self.assertFails(lambda o: o["listed"]["main"].update(landing_hits=1), "reached the fixture server")

    def test_a_subdomain_of_a_listed_domain_is_covered(self):
        def loaded(o):
            o["listed"]["subdomain"]["state"] = {"title": pp.LANDING_TITLE, "interstitial": False}
        self.assertFails(loaded, "subdomain of a listed domain: no interstitial")

    def test_go_back_is_the_primary_button_and_continue_is_secondary(self):
        self.assertFails(lambda o: o["listed"]["main"]["state"].update(primary_text="Continue anyway"), "not 'Go back'")
        self.assertFails(lambda o: o["listed"]["main"]["state"].update(proceed_link=False), "no 'Continue anyway'")
        self.assertFails(lambda o: o["listed"]["main"]["state"].update(details_closed=False), "not secondary")

    def test_go_back_must_return(self):
        self.assertFails(lambda o: o["listed"]["go_back"].update(back_on_clean=False), "did not return")

    def test_continue_anyway_must_load_the_page(self):
        self.assertFails(lambda o: o["listed"]["continue"].update(loaded=False), "did not load after the link")
        self.assertFails(lambda o: o["listed"]["continue"].update(landing_hits=0), "saw no request")

    def test_continue_anyway_is_remembered_in_the_tab_but_not_beyond_it(self):
        self.assertFails(lambda o: o["listed"]["continue"]["same_tab_again"].update(interstitial=True),
                         "not remembered in the same tab")
        self.assertFails(lambda o: o["listed"]["continue"].update(new_tab_title=pp.LANDING_TITLE),
                         "limited to its tab")

    def test_a_listed_subframe_must_be_blocked(self):
        def loaded(o):
            o["listed"]["subframe"]["frames"][1]["unreachable"] = None
        self.assertFails(loaded, "subframe: the listed host's frame was not blocked")
        self.assertFails(lambda o: o["listed"]["subframe"].update(listed_hits=1), "subframe: the request reached")

    def test_a_redirect_chain_is_stopped_at_the_listed_hop_in_both_frame_kinds(self):
        def main_passed(o):
            o["listed"]["redirect_main"]["state"] = {"title": pp.LANDING_TITLE, "interstitial": False}
        self.assertFails(main_passed, "redirect chain (main frame): no interstitial")
        self.assertFails(lambda o: o["listed"]["redirect_main"].update(listed_hits=1), "reached the fixture server")
        self.assertFails(lambda o: o["listed"]["redirect_main"].update(hop_hits=0), "never answered 302")

        def sub_passed(o):
            o["listed"]["redirect_subframe"]["frames"][1]["unreachable"] = None
        self.assertFails(sub_passed, "redirect chain (subframe): the listed hop was not blocked")
        self.assertFails(lambda o: o["listed"]["redirect_subframe"].update(listed_hits=1), "subframe): the listed target")

    def test_the_stop_must_be_the_protection_services(self):
        self.assertFails(lambda o: o["listed"]["histograms"].update(list_load=None), "no protection service")
        self.assertFails(lambda o: o["listed"]["histograms"].update(list_load={"true": 1, "false": 1}),
                         "did not load cleanly")
        self.assertFails(lambda o: o["listed"]["histograms"].update(blocked=None), "nothing shows")
        self.assertFails(lambda o: o["listed"]["histograms"].update(blocked={"true": 3, "false": 0}),
                         "something else stopped the rest")
        self.assertFails(lambda o: o["listed"]["histograms"].update(proceeded={"true": 1, "false": 0}),
                         "left no trace")

    def test_several_problems_are_all_named(self):
        def many(o):
            o["control"]["landing_hits"] = 0
            o["listed"]["go_back"]["back_on_clean"] = False
        status, detail = self.mutate(many)
        self.assertEqual(status, "fail")
        self.assertIn("2 problem(s)", detail)


class SetupTest(unittest.TestCase):
    def test_the_check_is_registered(self):
        self.assertIn("phishing_protection", REGISTRY)

    def test_every_listed_name_is_in_the_test_list_or_under_a_listed_parent(self):
        for key, host in pp.LISTED.items():
            covered = any(host == name or host.endswith("." + name) for name in pp.LIST_NAMES)
            self.assertTrue(covered, f"{key}: {host}")
        self.assertNotIn(pp.CLEAN, pp.LIST_NAMES)
        for name in pp.LIST_NAMES:
            self.assertGreaterEqual(name.count("."), 1)

    def test_names_are_exempt_from_https_only_mode(self):
        for host in [*pp.LISTED.values(), pp.CLEAN]:
            self.assertTrue(host.endswith(".localhost"), host)

    def test_the_switch_matches_the_browser_side(self):
        hook = (REPO / "overlay/components/aerie_adblock/browser/aerie_protection_hook.h").read_text()
        self.assertIn(pp.LIST_SWITCH.lstrip("-"), hook)

    def test_fixture_pages_exist_and_the_landing_page_tries_to_run_and_style(self):
        root = REPO / "tools/aerie_bench/fixtures/protection"
        landing = (root / "landing.html").read_text()
        self.assertIn(pp.LANDING_TITLE, landing)
        self.assertIn("window.blockedPageRan = true", landing)
        self.assertIn("rgb(0, 255, 0)", landing)
        self.assertIn("iframe", (root / "frame.html").read_text())

    def test_the_histogram_names_match_the_code(self):
        code = "".join(p.read_text() for p in (REPO / "overlay").rglob("*.cc"))
        for name in (pp.LOAD_HISTOGRAM, pp.BLOCKED_HISTOGRAM, pp.PROCEED_HISTOGRAM):
            self.assertIn(f'"{name}"', code)


class HopServerTest(unittest.TestCase):
    class NoFollow(urllib.request.HTTPRedirectHandler):
        def redirect_request(self, *a, **k):
            return None

    def get(self, server, path):
        opener = urllib.request.build_opener(self.NoFollow)
        try:
            with opener.open(server.base_url + path, timeout=5) as r:
                return r.status, r.headers.get("Location")
        except urllib.error.HTTPError as err:
            return err.code, err.headers.get("Location")

    def test_hop_redirects_to_a_loopback_fixture_host_and_the_server_logs_it(self):
        with fixtures.FixtureServer() as server:
            target = "http://chain.localhost:1234/protection/landing.html"
            status, location = self.get(server, "/protection/hop?to=" + urllib.parse.quote(target, safe=""))
            self.assertEqual((status, location), (302, target))
            self.assertEqual(server.snapshot()[0]["status"], 302)

    def test_hop_refuses_other_targets(self):
        with fixtures.FixtureServer() as server:
            for target in ("https://evil.example/", "http://evil.example/", "javascript:alert(1)", ""):
                status, location = self.get(server, "/protection/hop?to=" + urllib.parse.quote(target, safe=""))
                self.assertEqual(status, 400, target)
                self.assertIsNone(location)


if __name__ == "__main__":
    unittest.main()
