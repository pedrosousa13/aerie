"""Branding checks (issue #5): judge() against recorded observations, plus the page list."""

import re
import unittest
from pathlib import Path

from aerie_bench import checks
from aerie_bench.checks import branding

REPO = Path(__file__).resolve().parent.parent


def page(url, title="Page", text="Nothing to see", error=None):
    return {"url": url, "title": title, "text": text, "error": error}


CONTROL_HIT = page(branding.CONTROL_PATH, title="Control", text="Powered by Helium inside a shadow root")


def scan_obs(pages, control=CONTROL_HIT):
    return {"pages": pages, "control": control}


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

    def test_build_path_lines_are_exempt_on_the_version_and_about_pages_only(self):
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

    def test_pending_pages_may_still_say_helium_in_their_text_but_not_in_the_title(self):
        pending = next(iter(branding.PENDING_SERVICES_PAGES))
        status, detail = self.judge(scan_obs([page(pending, text="Helium services")]))
        self.assertEqual(status, "pass", detail)
        self.assertIn("#23", detail)
        status, detail = self.judge(scan_obs([page(pending, title="Helium services")]))
        self.assertEqual(status, "fail")

    def test_an_empty_scan_fails(self):
        self.assertEqual(self.judge(scan_obs([]))[0], "fail")


class PageListTest(unittest.TestCase):
    def test_the_list_covers_the_surfaces_the_issue_names(self):
        urls = set(branding.PAGES)
        for needed in ("chrome://version/", "chrome://settings/help", "chrome://settings/", "chrome://flags/",
                       "chrome://chrome-urls/"):
            self.assertIn(needed, urls)
        self.assertGreaterEqual(len(urls), 15)

    def test_pages_are_unique_internal_urls(self):
        self.assertEqual(len(branding.PAGES), len(set(branding.PAGES)))
        for url in branding.PAGES:
            self.assertRegex(url, r"^chrome://[a-z-]+/")

    def test_the_pending_pages_are_scanned_too(self):
        self.assertLessEqual(set(branding.PENDING_SERVICES_PAGES), set(branding.PAGES))

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
