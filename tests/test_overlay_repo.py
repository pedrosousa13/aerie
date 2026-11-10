"""The overlay in this repo: what it holds, and that its GN files name files that exist."""

import unittest
from pathlib import Path

from aerie_build import overlay

REPO = Path(__file__).resolve().parents[1]


class RepoOverlayTest(unittest.TestCase):
    def test_repo_overlay_has_no_build_artifacts_and_only_adds_aerie_paths(self):
        files = overlay.collect(overlay.overlay_root(REPO))
        self.assertTrue(files, "the repo overlay is empty")
        for rel in files:
            self.assertTrue(rel.startswith(("components/aerie_adblock/", "third_party/filter_lists/",
                                            "chrome/browser/aerie_adblock/",
                                            "chrome/app/aerie_services_strings.grdp",
                                            "chrome/browser/ui/views/aerie_adblock/",
                                            "chrome/browser/ui/webui/aerie_guard_settings/", "chrome/browser/ui/webui/aerie_guard/",
                                            "chrome/browser/resources/settings/aerie_guard_page/",
                                            "chrome/browser/resources/aerie_guard/",
                                            "chrome/browser/ui/webui/aerie_onboarding/", "components/aerie_onboarding/",
                                            "components/aerie_widevine/", "chrome/browser/aerie_widevine/",
                                            "components/aerie_fingerprint/", "components/aerie_gpu_info/", "components/aerie_screen/", "components/aerie_text_metrics/", "components/aerie_webgl_readback/", "components/aerie_fonts/", "components/aerie_design_tokens/",
                                            "chrome/browser/resources/settings/appearance_page/aerie_motion_row.",
                                            "tools/metrics/histograms/metadata/aerie/",
                                            "third_party/rust/chromium_crates_io/patches/")), rel)
            self.assertNotIn("/target/", rel)

    def test_every_gn_source_in_the_component_exists_in_the_overlay(self):
        import re
        files = overlay.collect(overlay.overlay_root(REPO))
        for gn in ("components/aerie_adblock/BUILD.gn", "components/aerie_adblock/browser/BUILD.gn",
                   "chrome/browser/aerie_adblock/BUILD.gn", "chrome/browser/ui/views/aerie_adblock/BUILD.gn",
                   "chrome/browser/ui/webui/aerie_guard_settings/BUILD.gn",
                   "components/aerie_widevine/BUILD.gn", "chrome/browser/aerie_widevine/BUILD.gn",
                   "components/aerie_design_tokens/BUILD.gn"):
            base = gn.rsplit("/", 1)[0]
            text = (overlay.overlay_root(REPO) / gn).read_text()
            for name in re.findall(r'"([A-Za-z0-9_./-]+\.(?:cc|h|rs|py|json|grd))"', text):
                if name.startswith(("//", "grit/")):  # labels, and files grit generates
                    continue
                self.assertIn(f"{base}/{name}", files, f"{gn} lists {name}, which is not in the overlay")


class GnLabelPatternTest(unittest.TestCase):
    def test_no_double_star_in_any_label_pattern(self):
        # In GN, "//dir/*" already means dir and every subdirectory. "**" is an
        # error ("'*' must match full directories") that stops gn gen.
        import re
        for gn in overlay.overlay_root(REPO).rglob("BUILD.gn"):
            for number, line in enumerate(gn.read_text().splitlines(), 1):
                code = line.split("#", 1)[0]
                for label in re.findall(r'"([^"]*\*\*[^"]*)"', code):
                    self.fail(f"{gn}:{number}: label pattern {label!r} uses '**'; use '/*'")

    def test_the_check_would_catch_it(self):
        import re
        self.assertTrue(re.findall(r'"([^"]*\*\*[^"]*)"', '"//components/aerie_adblock/**",'))

    def test_visibility_patterns_cover_the_subdirectories_that_use_them(self):
        text = (overlay.overlay_root(REPO) / "components/aerie_adblock/BUILD.gn").read_text()
        self.assertIn('"//components/aerie_adblock/*",', text)


class BrowserTargetDepsTest(unittest.TestCase):
    def test_headers_the_hook_exposes_come_from_public_deps(self):
        import re
        root = overlay.overlay_root(REPO) / "components/aerie_adblock/browser"
        gn = (root / "BUILD.gn").read_text()
        block = gn[gn.index('source_set("browser")'):gn.index('source_set("classified_request")')]
        public = re.search(r"public_deps = \[(.*?)\]", block, re.S).group(1)
        for needed in (":service", "//content/public/browser", "//services/network/public/cpp"):
            self.assertIn(f'"{needed}"', public)
        # What the public header includes, and the part's .cc (in another target) includes.
        hook_h = (root / "aerie_adblock_hook.h").read_text()
        self.assertIn("filter_engine_service.h", hook_h)
        self.assertIn("content/public/browser/content_browser_client.h", hook_h)
        self.assertIn("services/network/public/cpp/url_loader_factory_builder.h", hook_h)


class HistogramsTest(unittest.TestCase):
    def test_every_aerie_histogram_in_the_code_has_an_xml_entry_and_the_bench_names_match(self):
        import re
        root = overlay.overlay_root(REPO)
        used = set()
        for path in root.rglob("*.cc"):
            used |= set(re.findall(r'"(Aerie\.[A-Za-z.]+)"', path.read_text()))
        xml = (root / "tools/metrics/histograms/metadata/aerie/histograms.xml").read_text()
        declared = set(re.findall(r'<histogram name="([^"]+)"', xml))
        self.assertEqual(used, declared)
        self.assertEqual(sorted(declared), re.findall(r'<histogram name="([^"]+)"', xml), "keep histograms.xml sorted")
        gni = (REPO / "patches/adblock/0005-list-aerie-histograms.patch").read_text()
        self.assertIn("histograms/metadata/aerie/histograms.xml", gni)
        bench = "".join(p.read_text() for p in sorted((REPO / "tools/aerie_bench/checks").glob("*.py")))
        for name in used:
            self.assertIn(name, bench, f"the Bench check does not know {name}")


if __name__ == "__main__":
    unittest.main()
