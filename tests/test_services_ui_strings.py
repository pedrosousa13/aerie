"""The extension proxy and spell check strings (issue #23) are grd messages, and the patch that uses them is whole."""

import re
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
GRDP = REPO / "overlay" / "chrome" / "app" / "aerie_services_strings.grdp"
PATCHES = REPO / "patches" / "services"
UI_PATCH = PATCHES / "0006-remove-the-helium-services-ui.patch"
DIALOG_PATCH = PATCHES / "0003-extension-proxy-not-configured.patch"
COMPONENTS_PATCH = PATCHES / "0007-components-page-says-aerie.patch"
NAGS_PATCH = PATCHES / "0009-remove-the-helium-services-nags.patch"


def patch_text() -> str:
    """The patches that use the messages."""
    return "".join(p.read_text(encoding="utf-8") for p in (UI_PATCH, DIALOG_PATCH, COMPONENTS_PATCH, NAGS_PATCH))


def messages():
    return {m.get("name"): m for m in ET.parse(GRDP).getroot().iter("message")}


class StringsTest(unittest.TestCase):
    def test_every_message_has_a_description_and_text(self):
        for name, msg in messages().items():
            with self.subTest(name=name):
                self.assertTrue((msg.get("desc") or "").strip())
                self.assertTrue("".join(msg.itertext()).strip())

    def test_no_upstream_product_name(self):
        text = GRDP.read_text(encoding="utf-8")
        for word in ("Chrome", "Chromium", "Helium"):
            self.assertNotRegex(text, rf"\b{word}\b")

    def test_every_message_is_used_by_a_patch(self):
        used = patch_text()
        for name in messages():
            with self.subTest(name=name):
                self.assertRegex(used, rf"\b{name}\b")

    def test_every_message_a_patch_asks_for_exists(self):
        used = set(re.findall(r"\bIDS_(?:SETTINGS_)?AERIE_EXTENSION_PROXY_\w+|"
                              r"\bIDS_SETTINGS_LANGUAGES_DICTIONARY_DOWNLOAD_FAILED_HELP_AERIE\b|"
                              r"\bIDS_AERIE_COMPONENTS_SVC_STATUS_\w+|"
                              r"\bIDS_AERIE_DOWNLOAD_BUBBLE_EXTENSION_DOWNLOADS_OFF\b",
                              patch_text()))
        self.assertEqual(used, set(messages()))

    def test_the_part_is_included_by_the_grd_and_listed_in_the_gritdeps(self):
        text = UI_PATCH.read_text(encoding="utf-8")
        self.assertIn('+      <part file="aerie_services_strings.grdp" />', text)
        self.assertIn("+chrome/app/aerie_services_strings.grdp", text)


class DialogTest(unittest.TestCase):
    def test_the_dialog_text_is_not_a_literal(self):
        added = [l for l in DIALOG_PATCH.read_text(encoding="utf-8").splitlines() if l.startswith("+")]
        self.assertEqual([l for l in added if 'u"' in l], [])

    def test_the_dialog_does_not_point_at_the_removed_page(self):
        body = messages()["IDS_AERIE_EXTENSION_PROXY_UNSET_BODY"]
        self.assertNotIn("Services", "".join(body.itertext()))


class ComponentsPageTest(unittest.TestCase):
    """chrome://components after a failed component update (issue #131): Helium's status string
    names Helium services, which Aerie removed (#23), so the page shows an Aerie message instead."""

    def test_the_status_message_does_not_name_helium(self):
        msg = messages()["IDS_AERIE_COMPONENTS_SVC_STATUS_UPDATES_DISABLED"]
        self.assertNotRegex("".join(msg.itertext()), r"(?i)helium")

    def test_the_handler_shows_the_aerie_message_and_not_helium_s(self):
        text = COMPONENTS_PATCH.read_text(encoding="utf-8")
        self.assertIn("--- a/chrome/browser/ui/webui/components/components_handler.cc", text)
        self.assertRegex(text, r"(?m)^-\s+IDS_COMPONENTS_SVC_STATUS_ERROR_HELIUM_SERVICES\);")
        self.assertRegex(text, r"(?m)^\+\s+IDS_AERIE_COMPONENTS_SVC_STATUS_UPDATES_DISABLED\);")

    def test_the_patch_follows_the_helium_patch_it_edits(self):
        series = (REPO / "patches" / "series").read_text().splitlines()
        self.assertIn("services/0007-components-page-says-aerie.patch", [l.strip() for l in series])
        # The Helium patch comes from helium-adapt.txt and is applied before every series entry.
        self.assertIn("helium/core/component-updates.patch",
                      (REPO / "patches" / "helium-adapt.txt").read_text())


class PatchTest(unittest.TestCase):
    def test_the_patch_is_in_the_series_after_the_patches_it_edits(self):
        series = [l.strip() for l in (REPO / "patches" / "series").read_text().splitlines()
                  if l.strip() and not l.startswith("#")]
        ui = series.index("services/0006-remove-the-helium-services-ui.patch")
        for earlier in ("hardening/0003-hardening-settings-page.patch", "onboarding/0001-serve-aerie-setup-page.patch",
                        "services/0003-extension-proxy-not-configured.patch"):
            self.assertLess(series.index(earlier), ui)

    def test_the_patch_deletes_the_services_page_and_route(self):
        text = UI_PATCH.read_text(encoding="utf-8")
        for name in ("services_page.ts", "services_page.html"):
            self.assertRegex(text, rf"--- a/\S+/{name}\n\+\+\+ /dev/null")
        self.assertRegex(text, r"-  r\.HELIUM_SERVICES = ")


if __name__ == "__main__":
    unittest.main()
