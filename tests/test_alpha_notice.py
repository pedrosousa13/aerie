"""Alpha notice (issue #199): the setup page and About say Aerie is alpha and link the terms and privacy policy."""

import re
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
GRDP = REPO / "overlay" / "components" / "aerie_alpha_notice" / "aerie_alpha_notice_strings.grdp"
SETUP_JS = (REPO / "overlay/components/aerie_onboarding/resources/setup.js").read_text(encoding="utf-8")
SETUP_H = (REPO / "overlay/components/aerie_onboarding/aerie_onboarding_localized_strings.h").read_text(encoding="utf-8")
STRINGS_PATCH = REPO / "patches/branding/0007-add-the-alpha-notice-strings.patch"
ABOUT_PATCH = REPO / "patches/branding/0008-show-the-alpha-notice-on-about.patch"
SERIES = (REPO / "patches/series").read_text(encoding="utf-8")

NOTICE = "Aerie is alpha software and has not had an independent security audit."
TERMS = "https://github.com/pedrosousa13/aerie/blob/main/TERMS.md"
PRIVACY = "https://github.com/pedrosousa13/aerie/blob/main/PRIVACY.md"
IDS = ("IDS_AERIE_ALPHA_NOTICE", "IDS_AERIE_ALPHA_TERMS_LINK", "IDS_AERIE_ALPHA_PRIVACY_LINK")


def messages():
    root = ET.parse(GRDP).getroot()
    return {m.get("name"): (m.get("desc") or "", "".join(m.itertext()).strip()) for m in root.iter("message")}


class StringsTest(unittest.TestCase):
    def test_the_grdp_has_the_three_messages(self):
        msgs = messages()
        self.assertEqual(set(msgs), set(IDS))
        self.assertEqual(msgs["IDS_AERIE_ALPHA_NOTICE"][1], NOTICE)
        for name, (desc, _) in msgs.items():
            self.assertTrue(desc.strip(), name)

    def test_no_upstream_product_name(self):
        text = GRDP.read_text(encoding="utf-8")
        for word in ("Chrome", "Chromium", "Helium"):
            self.assertNotRegex(text, rf"\b{word}\b")

    def test_the_target_files_exist_for_the_links(self):
        self.assertTrue((REPO / "TERMS.md").is_file())
        self.assertTrue((REPO / "PRIVACY.md").is_file())


class SetupPageTest(unittest.TestCase):
    def test_header_lists_the_shared_messages(self):
        for name in IDS:
            self.assertIn(name, SETUP_H)

    def test_the_page_shows_the_notice_and_both_links(self):
        for key in ("alphaNotice", "alphaTerms", "alphaPrivacy"):
            self.assertRegex(SETUP_JS, rf"t\('{key}'\)|'{key}'")
        self.assertIn(TERMS, SETUP_JS)
        self.assertIn(PRIVACY, SETUP_JS)

    def test_links_open_only_on_click_in_a_new_tab(self):
        self.assertRegex(SETUP_JS, r"alphaLinks[\s\S]{0,400}target: '_blank'")
        self.assertRegex(SETUP_JS, r"rel: 'noopener noreferrer'")


class AboutTest(unittest.TestCase):
    def test_patches_are_in_the_series(self):
        self.assertIn("branding/0007-add-the-alpha-notice-strings.patch", SERIES)
        self.assertIn("branding/0008-show-the-alpha-notice-on-about.patch", SERIES)

    def test_strings_patch_includes_the_part_and_hands_each_message_over(self):
        text = STRINGS_PATCH.read_text(encoding="utf-8")
        self.assertIn("components/aerie_alpha_notice/aerie_alpha_notice_strings.grdp", text)
        for name in IDS:
            self.assertIn(name, text)
        for key in ("aerieAlphaNotice", "aerieAlphaTerms", "aerieAlphaPrivacy"):
            self.assertIn(key, text)

    def test_about_patch_shows_notice_under_the_version_with_links(self):
        text = ABOUT_PATCH.read_text(encoding="utf-8")
        self.assertIn("$i18n{aboutBrowserVersion}", text)
        self.assertIn("$i18n{aerieAlphaNotice}", text)
        self.assertIn(TERMS, text)
        self.assertIn(PRIVACY, text)
        self.assertEqual(len(re.findall(r'target="_blank"', text)), 2)


if __name__ == "__main__":
    unittest.main()
