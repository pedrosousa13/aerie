"""aerie://setup (issue #46): the page, its strings and the hook that serves it stay consistent."""

import re
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
COMPONENT = REPO / "overlay" / "components" / "aerie_onboarding"
RESOURCES = COMPONENT / "resources"
SCRIPT = (RESOURCES / "setup.js").read_text(encoding="utf-8")
HEADER = (COMPONENT / "aerie_onboarding_localized_strings.h").read_text(encoding="utf-8")
PATCH = REPO / "patches" / "onboarding" / "0001-serve-aerie-setup-page.patch"


def header_entries():
    return re.findall(r'\{"(\w+)", (IDS_AERIE_SETUP_\w+)\}', HEADER)


def grdp_messages():
    root = ET.parse(COMPONENT / "aerie_onboarding_strings.grdp").getroot()
    return {m.get("name"): m for m in root.iter("message")}


class StringsTest(unittest.TestCase):
    def test_header_and_grdp_list_the_same_messages(self):
        ids = [i for _, i in header_entries()]
        self.assertEqual(len(ids), len(set(ids)), "a message is listed twice in the header")
        self.assertEqual(set(ids), set(grdp_messages()))

    def test_every_message_has_a_description(self):
        for name, msg in grdp_messages().items():
            with self.subTest(name=name):
                self.assertTrue((msg.get("desc") or "").strip())

    def test_the_page_only_asks_for_listed_keys(self):
        keys = {k for k, _ in header_entries()}
        used = set(re.findall(r"\bt\('(\w+)'", SCRIPT))
        # Keys picked from tables in the script (colours, modes, import items, privacy rows...).
        used |= set(re.findall(r"'((?:color|look|import|privacy|searchDesc|block)[A-Z]\w*)'", SCRIPT))
        messages = set(re.findall(r"(?:chrome\.send|sendWithPromise)\('([\w.]+)'", SCRIPT))
        self.assertEqual(used - messages - keys, set())

    def test_no_product_name_from_upstream_in_strings(self):
        text = (COMPONENT / "aerie_onboarding_strings.grdp").read_text(encoding="utf-8")
        for word in ("Chrome", "Chromium", "Helium"):
            self.assertNotRegex(text, rf"\b{word}\b")


class PageTest(unittest.TestCase):
    def test_every_page_file_is_in_the_grd_and_gn_inputs(self):
        grd = ET.parse(RESOURCES / "aerie_onboarding_resources.grd").getroot()
        in_grd = {inc.get("file") for inc in grd.iter("include")}
        files = {p.name for p in RESOURCES.iterdir() if p.suffix != ".grd"}
        self.assertEqual(in_grd, files)
        gn = (COMPONENT / "BUILD.gn").read_text(encoding="utf-8")
        for name in files:
            self.assertIn(f'"resources/{name}"', gn)

    def test_index_is_the_resource_the_controller_asks_for(self):
        grd = (RESOURCES / "aerie_onboarding_resources.grd").read_text(encoding="utf-8")
        self.assertIn('name="IDR_AERIE_ONBOARDING_INDEX_HTML" file="index.html"', grd)
        self.assertIn("IDR_AERIE_ONBOARDING_INDEX_HTML", PATCH.read_text(encoding="utf-8"))

    def test_the_script_never_assigns_html(self):
        for pattern in ("innerHTML", "outerHTML", "insertAdjacentHTML", "document.write", "eval("):
            self.assertNotIn(pattern, SCRIPT)

    def test_the_page_never_consents_to_helium_services(self):
        code = re.sub(r"//.*", "", SCRIPT)
        self.assertFalse("user_consented" in code or "services." in code)

    def test_the_font_ships_with_its_licence(self):
        self.assertTrue((RESOURCES / "outfit.woff2").read_bytes().startswith(b"wOF2"))
        self.assertIn("SIL OPEN FONT LICENSE Version 1.1", (RESOURCES / "OFL.txt").read_text(encoding="utf-8"))

    def test_theme_ids_match_the_handler(self):
        handler = (REPO / "overlay/chrome/browser/ui/webui/aerie_onboarding/aerie_theme_handler.h").read_text(encoding="utf-8")
        native = set(re.findall(r'\{"(\w+)", (?:SkColorSetRGB|std::nullopt)', handler))
        block = SCRIPT[SCRIPT.index("const COLORS = {"):SCRIPT.index("};", SCRIPT.index("const COLORS = {"))]
        page = set(re.findall(r"^\s+(\w+): \[", block, re.M))
        self.assertEqual(native, page)
        self.assertEqual(len(native), 6)


class HookTest(unittest.TestCase):
    def test_no_reference_to_the_helium_page_is_left(self):
        added = [l for l in PATCH.read_text(encoding="utf-8").splitlines() if l.startswith("+") and not l.startswith("+++")]
        self.assertFalse([l for l in added if "helium_onboarding" in l])
        removed = [l for l in PATCH.read_text(encoding="utf-8").splitlines() if l.startswith("-") and not l.startswith("---")]
        self.assertTrue(any("svelte-trusted-html" in l for l in removed))

    def test_the_hook_is_in_the_series_and_the_old_ublock_patch_is_gone(self):
        series = (REPO / "patches" / "series").read_text(encoding="utf-8")
        self.assertIn("onboarding/0001-serve-aerie-setup-page.patch", series)
        self.assertNotIn("remove-onboarding-ublock-toggle", series)

    def test_helium_page_is_not_downloaded(self):
        from aerie_build import helium_deps
        self.assertIn("onboarding", helium_deps.DROPPED_SECTIONS)


if __name__ == "__main__":
    unittest.main()
