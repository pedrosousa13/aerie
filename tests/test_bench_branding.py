"""Branding checks (issue #5): judge() against recorded observations, plus the page list."""

import re
import unittest
from pathlib import Path
from unittest import mock

from aerie_bench import checks
from aerie_bench.checks import branding

REPO = Path(__file__).resolve().parent.parent


def page(url, title="Page", text="Nothing to see", error=None):
    return {"url": url, "title": title, "text": text, "error": error}


CONTROL_HIT = page(branding.CONTROL_PATH, title="Control", text="Powered by Helium inside a shadow root")


def failed_update(text="CRLSet\nStatus - Component updates are disabled in Aerie.", statuses=None, error=None):
    """chrome://components read after the Bench forced a component update that failed (issue #131)."""
    return {**page("chrome://components/ (after a failed update)", text=text, error=error),
            "statuses": ["Component updates are disabled in Aerie."] if statuses is None else statuses}


def failed_download(text="Downloads\nmissing.bin\nFailed - No file", failed=True, bubble=None, error=None):
    """chrome://downloads and the download bubble read after the Bench forced a download that failed (issue #94)."""
    return {**page("chrome://downloads/ (after a failed download)", text=text, error=error),
            "failed": failed,
            "bubble": {"texts": ["missing.bin", "Failed - No file"], "error": None} if bubble is None else bubble}


def scan_obs(pages, control=CONTROL_HIT, forced=None, download=None):
    return {"pages": pages, "control": control, "failed_update": forced or failed_update(),
            "failed_download": download or failed_download()}


class ScannerTest(unittest.TestCase):
    def test_finds_the_word_in_any_case_but_not_inside_other_words(self):
        for text in ("About Helium", "helium", "HELIUM browser", "Helium's", "(Helium)"):
            self.assertTrue(branding.find_helium(text), text)
        for text in ("Heliumite", "unhelium", "Aerie", "", "helium_services"):
            self.assertFalse(branding.find_helium(text), text)


class ScrubTest(unittest.TestCase):
    PATHS = "Command Line\t/x/helium-1/out/Default/aerie --flag\nExecutable Path\t/x/helium-1/aerie\nProfile Path\t/h/helium\n"

    def test_flag_ids_are_exempt_on_the_flags_page_only(self):
        self.assertEqual(branding.scrub("Noise #helium-x", "chrome://flags/").strip(), "Noise")
        for url in ("chrome://settings/", "chrome://version/", "chrome://flags/other"):
            self.assertTrue(branding.find_helium(branding.scrub("Noise #helium-x", url)), url)

    def test_build_path_lines_are_exempt_on_the_version_page_only(self):
        self.assertFalse(branding.find_helium(branding.scrub(self.PATHS, "chrome://version/")))
        for url in ("chrome://flags/", "chrome://settings/help", "chrome://management/"):
            self.assertTrue(branding.find_helium(branding.scrub(self.PATHS, url)), url)

    def test_the_helium_authors_credit_is_exempt_on_the_version_and_about_pages_only(self):
        line = "Copyright 2026 The Aerie Authors, The Helium Authors and The Chromium Authors."
        self.assertFalse(branding.find_helium(branding.scrub(line, "chrome://version/")))
        self.assertFalse(branding.find_helium(branding.scrub(line, "chrome://settings/help")))
        self.assertTrue(branding.find_helium(branding.scrub(line, "chrome://settings/privacy")))
        self.assertTrue(branding.find_helium(branding.scrub("The Helium team", "chrome://settings/help")))
        self.assertTrue(branding.find_helium(branding.scrub("The Helium team", "chrome://version/")))


class NoHeliumJudgeTest(unittest.TestCase):
    judge = staticmethod(checks.get("branding_no_helium").judge)

    def test_clean_pages_pass(self):
        status, detail = self.judge(scan_obs([page("chrome://version/"), page("chrome://flags/")]))
        self.assertEqual(status, "pass", detail)
        self.assertIn("2 pages", detail)

    def test_text_that_says_helium_fails_and_names_the_page(self):
        status, detail = self.judge(scan_obs([page("chrome://version/", text="Helium Authors")]))
        self.assertEqual(status, "fail")
        self.assertIn("chrome://version/", detail)

    def test_a_title_that_says_helium_fails(self):
        status, detail = self.judge(scan_obs([page("chrome://flags/", title="Helium Experiments")]))
        self.assertEqual(status, "fail")
        self.assertIn("title", detail)

    def test_a_page_that_did_not_load_fails_because_it_proves_nothing(self):
        status, detail = self.judge(scan_obs([page("chrome://about/", error="net::ERR_FAILED")]))
        self.assertEqual(status, "fail")
        self.assertIn("did not load", detail)

    def test_a_scanner_that_misses_the_control_page_fails(self):
        status, detail = self.judge(scan_obs([page("chrome://version/")],
                                             control=page(branding.CONTROL_PATH, text="clean")))
        self.assertEqual(status, "fail")
        self.assertIn("negative control failed", detail)

    def test_a_missing_control_page_fails(self):
        status, detail = self.judge(scan_obs([page("chrome://version/")],
                                             control=page(branding.CONTROL_PATH, error="404")))
        self.assertEqual(status, "fail")
        self.assertIn("negative control failed", detail)

    def test_the_privacy_and_languages_pages_are_judged_like_every_other_page(self):
        # They used to be exempt while Helium services text was on them (issue #23).
        for url in ("chrome://settings/privacy", "chrome://settings/languages"):
            with self.subTest(url=url):
                status, detail = self.judge(scan_obs([page(url, text="Helium services")]))
                self.assertEqual(status, "fail")
                self.assertIn(url, detail)

    def test_an_empty_scan_fails(self):
        self.assertEqual(self.judge(scan_obs([]))[0], "fail")

    def test_helium_wording_after_a_failed_update_fails(self):
        # Helium's string for a failed component update (issue #131).
        text = "CRLSet\nComponent updates are disabled. See Helium services in settings."
        status, detail = self.judge(scan_obs([page("chrome://version/")], forced=failed_update(
            text=text, statuses=["Component updates are disabled. See Helium services in settings."])))
        self.assertEqual(status, "fail")
        self.assertIn("failed", detail)
        self.assertIn("Helium", detail)

    def test_a_forced_update_that_never_reached_the_disabled_status_proves_nothing(self):
        # A plain "Update error" is another status: the gate must not pass on it.
        for statuses in ([], ["Up-to-date"], ["Checking for update..."], ["Update error"]):
            with self.subTest(statuses=statuses):
                status, detail = self.judge(scan_obs([page("chrome://version/")],
                                                     forced=failed_update(statuses=statuses)))
                self.assertEqual(status, "fail")
                self.assertIn("did not fail", detail)

    def test_a_missing_failed_update_case_fails(self):
        obs = scan_obs([page("chrome://version/")])
        del obs["failed_update"]
        self.assertEqual(self.judge(obs)[0], "fail")

    def test_a_failed_update_page_that_did_not_load_fails(self):
        status, detail = self.judge(scan_obs([page("chrome://version/")], forced=failed_update(error="load timeout")))
        self.assertEqual(status, "fail")
        self.assertIn("did not load", detail)

    def test_the_aerie_wording_must_be_on_the_page(self):
        status, detail = self.judge(scan_obs([page("chrome://version/")], forced=failed_update(
            text="CRLSet\nStatus - Component updates are disabled.")))
        self.assertEqual(status, "fail")
        self.assertIn("Component updates are disabled in Aerie.", detail)

    def test_the_aerie_wording_after_a_failed_update_passes(self):
        self.assertEqual(self.judge(scan_obs([page("chrome://version/")]))[0], "pass")


class FailedDownloadJudgeTest(unittest.TestCase):
    """The download bubble and chrome://downloads after a download that failed (issue #94)."""
    judge = staticmethod(checks.get("branding_no_helium").judge)

    def verdict(self, **kw):
        return self.judge(scan_obs([page("chrome://version/")], download=failed_download(**kw)))

    def test_a_clean_bubble_and_page_pass(self):
        status, detail = self.verdict()
        self.assertEqual(status, "pass", detail)
        self.assertIn("download bubble", detail)

    def test_helium_services_wording_in_the_bubble_fails_and_names_the_bubble(self):
        status, detail = self.verdict(bubble={"texts": ["Extension downloads are disabled. Enable this feature in "
                                                         "Helium services settings and try again."], "error": None})
        self.assertEqual(status, "fail")
        self.assertIn("download bubble", detail)
        self.assertIn("Helium", detail)

    def test_helium_wording_on_the_downloads_page_fails(self):
        status, detail = self.verdict(text="Downloads\nReview Helium services updates")
        self.assertEqual(status, "fail")
        self.assertIn("chrome://downloads", detail)

    def test_a_download_that_did_not_fail_proves_nothing(self):
        status, detail = self.verdict(failed=False)
        self.assertEqual(status, "fail")
        self.assertIn("did not fail", detail)

    def test_a_bubble_that_was_not_read_proves_nothing(self):
        for bubble in ({"texts": [], "error": None}, {"texts": [], "error": "UiDevToolsError: no port"}):
            with self.subTest(bubble=bubble):
                status, detail = self.verdict(bubble=bubble)
                self.assertEqual(status, "fail")
                self.assertIn("download bubble", detail)

    def test_a_downloads_page_that_did_not_load_fails(self):
        status, detail = self.verdict(error="load timeout")
        self.assertEqual(status, "fail")
        self.assertIn("did not complete", detail)

    def test_a_missing_failed_download_case_fails(self):
        obs = scan_obs([page("chrome://version/")])
        del obs["failed_download"]
        self.assertEqual(self.judge(obs)[0], "fail")


class FakeUi:
    """A UI DevTools tree: nodes are (id, class, props, children)."""

    def __init__(self, tree):
        self.tree, self.props = tree, {}

    def send(self, method, params=None):
        def build(n):
            return {"nodeId": n[0], "attributes": ["class", n[1]], "children": [build(c) for c in n[3]]}

        def collect(n):
            self.props[n[0]] = n[2]
            for c in n[3]:
                collect(c)
        collect(self.tree)
        return {"root": build(self.tree)}

    def properties(self, node_id):
        return self.props[node_id]


class BubbleTextTest(unittest.TestCase):
    def test_reads_only_the_views_below_the_download_bubble(self):
        tree = (1, "RootView", {}, [
            (2, "ToolbarView", {"Text": "Helium toolbar"}, []),
            (3, "DownloadBubbleContentsView", {"Text": "Downloads"}, [
                (4, "DownloadBubbleRowView", {"TooltipText": "missing.bin"}, [
                    (5, "Label", {"Text": "Failed - No file"}, [])])])])
        self.assertEqual(branding.bubble_texts(FakeUi(tree)), ["Downloads", "missing.bin", "Failed - No file"])

    def test_no_bubble_gives_no_text(self):
        self.assertEqual(branding.bubble_texts(FakeUi((1, "RootView", {}, [(2, "Label", {"Text": "x"}, [])]))), [])


class WiringTest(unittest.TestCase):
    """observe() runs every case and hands each one's data to judge() under the key judge() reads."""

    def test_observe_runs_the_pages_the_failed_update_and_the_failed_download(self):
        calls = []

        def fake_read_page(session, url, label=None, before_read=None):
            calls.append(url)
            return CONTROL_HIT if url.endswith(branding.CONTROL_PATH) else page(url)

        class Fixtures:
            base_url = "http://127.0.0.1:1"

        class FakeSession:
            fixtures = Fixtures()

        with mock.patch.object(branding, "read_page", fake_read_page), \
                mock.patch.object(branding, "read_failed_update", lambda s: failed_update()), \
                mock.patch.object(branding, "read_failed_download", lambda s: failed_download()):
            obs = checks.get("branding_no_helium")().observe(FakeSession())
        self.assertEqual(set(obs), {"control", "pages", "failed_update", "failed_download"})
        self.assertEqual(calls[1:], list(branding.PAGES))
        self.assertEqual(checks.get("branding_no_helium").judge(obs)[0], "pass")


class PageListTest(unittest.TestCase):
    def test_the_list_covers_the_surfaces_the_issue_names(self):
        urls = set(branding.PAGES)
        for needed in ("chrome://version/", "chrome://settings/help", "chrome://settings/", "chrome://flags/",
                       "chrome://chrome-urls/"):
            self.assertIn(needed, urls)
        self.assertGreaterEqual(len(urls), 15)

    def test_the_removed_helium_services_route_is_not_listed(self):
        # Issue #23 removed chrome://settings/privacy/services; the page would only show a 404.
        self.assertNotIn("chrome://settings/privacy/services", branding.PAGES)
        self.assertFalse([u for u in branding.PAGES if u.rstrip("/").endswith("/services")])

    def test_pages_are_unique_internal_urls(self):
        self.assertEqual(len(branding.PAGES), len(set(branding.PAGES)))
        for url in branding.PAGES:
            self.assertRegex(url, r"^chrome://[a-z-]+/")

    def test_no_page_is_exempt_from_the_text_scan(self):
        self.assertFalse(hasattr(branding, "PENDING_SERVICES_PAGES"))
        self.assertNotIn("PENDING_SERVICES_PAGES", Path(branding.__file__).read_text())

    def test_the_control_page_exists_and_says_helium(self):
        path = REPO / "tools/aerie_bench/fixtures" / branding.CONTROL_PATH.lstrip("/")
        self.assertTrue(re.search(r"helium", path.read_text(), re.I))


class SchemeJudgeTest(unittest.TestCase):
    judge = staticmethod(checks.get("branding_scheme").judge)

    def obs(self, aerie_url="chrome://version/", aerie_title="About Version",
            helium_url="about:blank", helium_title="", bogus_url="about:blank", bogus_title=""):
        return {"aerie": {"url": aerie_url, "title": aerie_title},
                "helium": {"url": helium_url, "title": helium_title},
                "bogus": {"url": bogus_url, "title": bogus_title}}

    def test_aerie_loads_and_helium_does_not(self):
        status, detail = self.judge(self.obs())
        self.assertEqual(status, "pass", detail)

    def test_aerie_url_may_stay_aerie(self):
        self.assertEqual(self.judge(self.obs(aerie_url="aerie://version/"))[0], "pass")

    def test_aerie_that_does_not_load_the_version_page_fails(self):
        status, detail = self.judge(self.obs(aerie_url="about:blank", aerie_title=""))
        self.assertEqual(status, "fail")
        self.assertIn("aerie://version", detail)

    def test_helium_that_still_loads_the_version_page_fails(self):
        status, detail = self.judge(self.obs(helium_url="chrome://version/", helium_title="About Version"))
        self.assertEqual(status, "fail")
        self.assertIn("helium://", detail)

    def test_a_helium_url_kept_as_a_registered_scheme_fails(self):
        status, _ = self.judge(self.obs(helium_url="helium://version/", helium_title="About Version"))
        self.assertEqual(status, "fail")

    def test_negative_control_an_unknown_scheme_must_look_like_helium(self):
        # If an invented scheme loads a page, the probe cannot tell "not registered" from "loads anything".
        status, detail = self.judge(self.obs(bogus_url="chrome://version/", bogus_title="About Version"))
        self.assertEqual(status, "fail")
        self.assertIn("negative control failed", detail)


class VersionJudgeTest(unittest.TestCase):
    judge = staticmethod(checks.get("branding_version").judge)

    def obs(self, about="Version 0.1.0 (Official Build, Chromium 157.0.8103.38) (64-bit)",
            version_page="Aerie 0.1.0 (Official Build) Chromium 157.0.8103.38", release="0.1.0",
            chromium="157.0.8103.38", helium="0.21.1.1"):
        return {"about_text": about, "version_page_text": version_page, "aerie_release": release,
                "chromium_version": chromium, "helium_version": helium}

    def test_both_versions_in_about_pass(self):
        self.assertEqual(self.judge(self.obs())[0], "pass")

    def test_missing_aerie_release_fails(self):
        status, detail = self.judge(self.obs(about="Version 0.21.1.1 (Official Build, Chromium 157.0.8103.38)"))
        self.assertEqual(status, "fail")
        self.assertIn("0.1.0", detail)

    def test_missing_chromium_version_fails(self):
        status, detail = self.judge(self.obs(about="Version 0.1.0 (Official Build)"))
        self.assertEqual(status, "fail")
        self.assertIn("157.0.8103.38", detail)

    def test_helium_s_version_number_in_about_fails(self):
        status, detail = self.judge(self.obs(about="Version 0.1.0 and 0.21.1.1 (Chromium 157.0.8103.38)"))
        self.assertEqual(status, "fail")
        self.assertIn("0.21.1.1", detail)

    def test_the_release_is_a_whole_number_not_a_prefix_of_another(self):
        status, _ = self.judge(self.obs(about="Version 10.1.0 (Chromium 157.0.8103.38)"))
        self.assertEqual(status, "fail")

    def test_the_version_page_must_carry_it_too(self):
        status, detail = self.judge(self.obs(version_page="Aerie (Official Build)"))
        self.assertEqual(status, "fail")
        self.assertIn("chrome://version", detail)

    def test_expected_numbers_come_from_the_repo(self):
        expected = branding.expected_versions()
        self.assertEqual(expected["aerie_release"], (REPO / "AERIE_VERSION").read_text().strip())
        self.assertRegex(expected["chromium_version"], r"^\d+\.\d+\.\d+\.\d+$")
        self.assertRegex(expected["helium_version"], r"^\d+\.\d+\.\d+\.\d+$")


if __name__ == "__main__":
    unittest.main()


class PressDownloadButtonTest(unittest.TestCase):
    class Ui(FakeUi):
        def __init__(self, tree):
            super().__init__(tree)
            self.pressed = []

        def press(self, node_id, props):
            self.pressed.append(node_id)

    def tree(self, tooltip="New download available"):
        return (1, "RootView", {}, [
            (2, "PinnedActionToolbarButton", {"TooltipText": "Download notes"}, []),  # a decoy with the word
            (3, "PinnedActionToolbarButton", {"TooltipText": tooltip}, [
                (4, "DownloadProgressRing", {}, [])])])

    def test_presses_the_button_that_holds_the_progress_ring_in_any_language(self):
        for tooltip in ("New download available", "Novo download disponivel", ""):
            with self.subTest(tooltip=tooltip):
                ui = self.Ui(self.tree(tooltip))
                self.assertTrue(branding.press_download_button(ui))
                self.assertEqual(ui.pressed, [3])

    def test_no_download_button_is_pressed_when_the_browser_made_none(self):
        ui = self.Ui((1, "RootView", {}, [(2, "PinnedActionToolbarButton", {"TooltipText": "Download"}, [])]))
        self.assertFalse(branding.press_download_button(ui))
        self.assertEqual(ui.pressed, [])


class DeadlineTest(unittest.TestCase):
    def test_cap_is_the_smaller_of_the_wish_and_what_is_left(self):
        now = [100.0]
        d = branding.Deadline(10, clock=lambda: now[0])
        self.assertEqual(d.cap(3), 3)
        now[0] = 108.0
        self.assertEqual(d.cap(3), 2.0)
        d.check("a step")
        now[0] = 111.0
        self.assertEqual(d.cap(3), 0.0)
        with self.assertRaisesRegex(branding.StepTimeout, "a step.*10s"):
            d.check("a step")

    def test_a_tree_walk_stops_at_the_deadline(self):
        tree = (1, "RootView", {}, [(2, "DownloadBubbleContentsView", {"Text": "x"}, [])])
        with self.assertRaises(branding.StepTimeout):
            branding.bubble_texts(FakeUi(tree), branding.Deadline(0))

    def test_describe_keeps_the_root_cause(self):
        try:
            try:
                raise OSError("port file missing")
            except OSError as inner:
                raise RuntimeError("cannot read the bubble") from inner
        except RuntimeError as exc:
            text = branding.describe(exc)
        self.assertEqual(text, "RuntimeError: cannot read the bubble <- OSError: port file missing")


class ForceFailureTest(unittest.TestCase):
    class Session:
        def __init__(self, done=True):
            self.done, self.log = done, []

        def wait_for(self, js, timeout=0, interval=0):
            self.log.append(("wait", js, timeout))
            return True if js == "ready" else self.done

        def evaluate(self, js, user_gesture=False):
            self.log.append(("run", js, user_gesture))

    def test_waits_for_ready_runs_the_action_then_waits_for_done(self):
        s = self.Session()
        self.assertTrue(branding.force_failure(s, "act", ready_js="ready", done_js="done", wait=7, user_gesture=True))
        self.assertEqual(s.log, [("wait", "ready", 15.0), ("run", "act", True), ("wait", "done", 7)])

    def test_a_failure_that_never_shows_is_false_not_an_error(self):
        self.assertFalse(branding.force_failure(self.Session(done=None), "act", done_js="done"))

    def test_without_a_done_check_it_just_runs_the_action(self):
        s = self.Session()
        self.assertTrue(branding.force_failure(s, "act"))
        self.assertEqual(s.log, [("run", "act", False)])

    def test_the_failed_update_and_the_failed_download_share_it(self):
        src = Path(branding.__file__).read_text()
        self.assertEqual(src.count("force_failure("), 3)  # the definition and its two callers


class ReadFailedDownloadTest(unittest.TestCase):
    def test_a_browser_that_cannot_start_leaves_the_root_cause_in_the_error(self):
        class Session:
            def new_profile(self):
                raise OSError("no space left")

        with mock.patch.object(branding.xinput, "xvfb", lambda timeout=15: mock.MagicMock()):
            got = branding.read_failed_download(Session())
        self.assertIn("could not force a failed download", got["error"])
        self.assertIn("OSError: no space left", got["error"])

    def test_a_case_that_runs_over_its_budget_fails_with_the_step(self):
        with mock.patch.object(branding.xinput, "xvfb", lambda timeout=15: mock.MagicMock()):
            got = branding.read_failed_download(mock.MagicMock(), budget=0)
        self.assertIn("timed out", got["error"])
        self.assertIn("starting the browser", got["error"])
        self.assertIn("did not complete", branding.judge_failed_download(got)[0])
