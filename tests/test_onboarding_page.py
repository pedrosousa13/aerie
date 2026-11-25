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
        keys = set(re.findall(r'\{"(\w+)", IDS_\w+\}', HEADER))
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

    def test_engine_ids_stay_strings(self):
        # Ids look like "db:3"; a Number() makes NaN and the handler check-fails.
        self.assertNotIn("Number(val)", SCRIPT)
        self.assertNotIn("askedDefault", SCRIPT)
        self.assertIn("'search-engines-changed'", SCRIPT)

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
        self.assertNotIn("graphite", native)

    def test_neutral_comes_first_and_is_the_default(self):
        block = SCRIPT[SCRIPT.index("const COLORS = {"):SCRIPT.index("};", SCRIPT.index("const COLORS = {"))]
        ids = re.findall(r"^\s+(\w+): \[", block, re.M)
        self.assertEqual(ids[0], "neutral")
        self.assertIn("color: 'neutral'", SCRIPT)
        self.assertRegex(SCRIPT, r"mode: 'system', color: 'neutral'")

    def test_neutral_is_the_plain_grayscale_default_and_never_calls_use_default_theme(self):
        # Neutral is the one choice with no seed: no user colour and the grayscale pref on, which is
        # Helium's default for a new profile. Grayscale off without a colour is Chromium's blue baseline
        # palette, so it must not be used for Neutral. UseDefaultTheme() also resets the appearance.
        # The run-time proof is the Bench check setup_theme (tests/test_bench_setup_theme.py).
        handler = (REPO / "overlay/chrome/browser/ui/webui/aerie_onboarding/aerie_theme_handler.h").read_text(encoding="utf-8")
        source = re.sub(r"//.*", "", (REPO / "overlay/chrome/browser/ui/webui/aerie_onboarding/aerie_theme_handler.cc").read_text(encoding="utf-8"))
        self.assertRegex(handler, r'\{"neutral", std::nullopt\}')
        self.assertNotIn("UseDefaultTheme()", source)
        self.assertNotIn("baseline", source)
        self.assertRegex(source, r"else \{\s*themes->SetIsGrayscale\(true\);\s*\}")
        self.assertEqual(source.count("SetIsGrayscale(true)"), 1)


class HookTest(unittest.TestCase):
    def test_helium_message_handler_is_replaced_by_the_setup_handler(self):
        # Issue #69: Helium's handler sets any helium.* pref and installs extensions.
        text = PATCH.read_text(encoding="utf-8")
        added = [l for l in text.splitlines() if l.startswith("+") and not l.startswith("+++")]
        removed = [l for l in text.splitlines() if l.startswith("-") and not l.startswith("---")]
        self.assertTrue(any("OnboardingMessageHandler" in l for l in removed))
        self.assertTrue(any("AerieSetupHandler" in l for l in added))
        self.assertFalse([l for l in added if "OnboardingMessageHandler" in l])

    def test_setup_handler_registers_only_what_the_page_uses(self):
        source = (REPO / "overlay/chrome/browser/ui/webui/aerie_onboarding/aerie_setup_handler.cc").read_text(encoding="utf-8")
        registered = set(re.findall(r'RegisterMessageCallback\(\s*"([\w.]+)"', source))
        # Issue #91 added the icon lookup; every addition to this set is a decision about what the page may ask.
        self.assertEqual(registered, {"setPref", "acceptLatestSchema", "aerieSetup.getBrowserIcons"})
        used = set(re.findall(r"(?:chrome\.send|sendWithPromise)\('([\w.]+)'", SCRIPT))
        self.assertTrue(registered <= used)
        for banned in ("getExtensions", "installExtension"):
            self.assertNotIn(banned, SCRIPT)

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


class BrowserIconTest(unittest.TestCase):
    """Issue #91: real browser icons in the import step, as data URLs only, with the letter tile as the fallback."""

    def test_the_page_asks_for_the_icons_of_the_listed_browsers(self):
        self.assertIn("sendWithPromise('aerieSetup.getBrowserIcons', S.profiles.map(p => p.name))", SCRIPT)

    def test_only_a_png_data_url_is_ever_used_as_an_icon(self):
        match = re.search(r"const PNG_DATA_URL = (/.*/);", SCRIPT)
        self.assertTrue(match, "the page must check the icon's form")
        self.assertIn("^data:image\\/png;base64,", match.group(1))
        self.assertIn("PNG_DATA_URL.test(url)", SCRIPT)
        # The one <img> the page builds takes its source from fillLogo(), whose icon comes from browserIcon(),
        # which applies that check.
        sources = re.findall(r"\bsrc\b\s*[:=]\s*([^,}\n]+)", SCRIPT)
        self.assertEqual([source.strip() for source in sources], ["it.icon"])
        self.assertEqual(len(re.findall(r"icon: browserIcon\(p\.name\)", SCRIPT)), 2)  # the rows and the hand-off logo

    def test_no_icon_means_the_letter_tile(self):
        self.assertIn("BROWSER_TILES[n % BROWSER_TILES.length], letter:", SCRIPT)
        self.assertIn("node.style.background = it.icon ? 'transparent' : it.tile;", SCRIPT)
        self.assertIn("it.icon ? h('img', {src: it.icon, alt: '', draggable: 'false'}) : it.letter", SCRIPT)

    def test_the_page_makes_no_network_request_for_icons(self):
        # The icon arrives as bytes from the browser; nothing on the page fetches one.
        for call in ("fetch(", "XMLHttpRequest", "WebSocket", "sendBeacon", "new Image", "EventSource", "importScripts"):
            self.assertNotIn(call, SCRIPT, call)
        html = (RESOURCES / "index.html").read_text(encoding="utf-8")
        css = (RESOURCES / "setup.css").read_text(encoding="utf-8")
        for text in (html, css):
            self.assertNotRegex(text, r"(?i)(?:src|href)\s*=\s*[\"']?https?:")
            self.assertNotRegex(text, r"(?i)url\(\s*[\"']?https?:")
            self.assertNotRegex(text, r"(?i)@import")
        # The page's content security policy lets images come from data: URLs and chrome:// only, and connect to itself.
        self.assertNotRegex(SCRIPT, r"https?://[^'\"`\s]*\.(png|jpg|jpeg|gif|svg|ico|webp)")

    def test_the_step_says_passwords_and_autofill_are_not_imported(self):
        self.assertIn("t('importNoPasswords')", SCRIPT)
        grdp = (COMPONENT / "aerie_onboarding_strings.grdp").read_text(encoding="utf-8")
        match = re.search(r'name="IDS_AERIE_SETUP_IMPORT_NO_PASSWORDS"[^>]*>\s*(.*?)\s*</message>', grdp, re.S)
        self.assertTrue(match)
        # No CSV route: Helium turns the password manager off by policy (credentials_enable_service is forced
        # off), which hides its "Import passwords" row, so the note must not promise one.
        self.assertNotIn("CSV", match.group(1))
        self.assertIn("not imported", match.group(1))
        self.assertIn("does not save passwords", match.group(1))

    def test_the_icon_resolver_reads_files_only(self):
        for name in ("browser_icon.cc", "import_sources.cc"):
            source = re.sub(r"//.*", "", (REPO / "overlay/components/aerie_import_sources" / name).read_text(encoding="utf-8"))
            for word in ("network", "URLLoader", "GURL", "http://", "https://", "socket", "SimpleURLLoader"):
                self.assertNotIn(word, source, f"{name}: {word}")
        handler = re.sub(r"//.*", "", (REPO / "overlay/chrome/browser/ui/webui/aerie_onboarding/aerie_setup_handler.cc").read_text(encoding="utf-8"))
        for word in ("network", "URLLoader", "SimpleURLLoader", "GURL"):
            self.assertNotIn(word, handler, word)
