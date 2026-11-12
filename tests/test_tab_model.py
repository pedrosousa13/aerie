"""The Tab Model (issue #176): source checks for the parts the C++ unit tests cannot see, such as the pref names in
the settings_private allowlist, the series, the GN wiring and the written decision about the Chromium pinned bit.
The C++ itself is built and run by `./aerie test-cpp` (aerie_tab_model_unittests)."""

import re
import unittest
from pathlib import Path

from aerie_build import pipeline

REPO = Path(__file__).resolve().parent.parent
COMPONENT = REPO / "overlay" / "components" / "aerie_tab_model"
SERVICE = REPO / "overlay" / "chrome" / "browser" / "aerie_tab_model"
SERIES = [l.strip() for l in (REPO / "patches" / "series").read_text().splitlines()
          if l.strip() and not l.startswith("#")]
PREFS_PATCH = "sidebar/0010-register-tab-model-prefs.patch"
SERVICE_PATCH = "sidebar/0011-create-tab-model-service.patch"


def code(path: Path) -> str:
    return re.sub(r"//[^\n]*", "", path.read_text(encoding="utf-8"))


def gn_sources(gn: Path) -> list[str]:
    return re.findall(r'"([A-Za-z0-9_./-]+\.(?:cc|h|inc))"', gn.read_text())


class BuildWiringTest(unittest.TestCase):
    def test_the_unit_test_binary_is_built_and_run_by_test_cpp(self):
        self.assertIn(("aerie_tab_model_unittests", ()), pipeline.TEST_BINARIES)

    def test_every_source_the_gn_files_name_exists(self):
        for directory in (COMPONENT, SERVICE):
            for name in gn_sources(directory / "BUILD.gn"):
                self.assertTrue((directory / name).is_file(), f"{directory.name}/BUILD.gn lists {name}")

    def test_every_unit_test_file_is_in_the_test_target(self):
        listed = set(gn_sources(COMPONENT / "BUILD.gn"))
        for path in COMPONENT.glob("*_unittest.cc"):
            self.assertIn(path.name, listed)

    def test_every_other_source_file_is_in_a_target(self):
        listed = set(gn_sources(COMPONENT / "BUILD.gn"))
        for path in list(COMPONENT.glob("*.cc")) + list(COMPONENT.glob("*.h")):
            self.assertIn(path.name, listed)
        listed = set(gn_sources(SERVICE / "BUILD.gn"))
        for path in SERVICE.glob("*.cc"):
            if path.name.endswith("browsertest.cc"):
                continue
            self.assertIn(path.name, listed)

    def test_the_browser_test_waits_for_the_aerie_browser_test_target(self):
        test = SERVICE / "tab_model_browsertest.cc"
        self.assertTrue(test.is_file())
        # The sleep controller's test (#179) is listed; this one is not.
        self.assertNotIn("tab_model_browsertest", (SERVICE / "BUILD.gn").read_text().replace("tab_model_browsertest.cc is not listed", ""))
        self.assertIn("#175", test.read_text())

    def test_an_off_the_record_profile_has_an_own_in_memory_service(self):
        # In Chromium's profile_selections.cc an Incognito or other off-the-record
        # profile takes the *regular* selection, so kOwnInstance covers both.
        factory = (SERVICE / "tab_model_service_factory.cc").read_text()
        self.assertIn("WithRegular(ProfileSelection::kOwnInstance)", factory)
        self.assertNotIn("kRedirectedToOriginal", factory)
        self.assertNotIn("kOriginalOnly", factory)
        self.assertNotIn("BuildForRegularProfile", factory)
        self.assertNotIn("WithGuest", code(SERVICE / "tab_model_service_factory.cc"))
        self.assertIn("profile_selections.cc", factory)
        service = code(SERVICE / "tab_model_service.cc")
        self.assertRegex(service, r"IsOffTheRecord\(\)\) \{\s*return base::FilePath\(\);")
        test = (SERVICE / "tab_model_browsertest.cc").read_text()
        self.assertIn("CreateIncognitoBrowser", test)
        self.assertIn("OffTheRecord", test)
        self.assertIn("persists_to_disk", test)
        doc = (REPO / "docs" / "sidebar-design.md").read_text()
        self.assertIn("An off-the-record profile (an Incognito window) has its own Tab Model", doc)

    def test_every_included_chromium_header_has_its_target_in_deps(self):
        # gn check runs only in the train, so the includes are matched with labels here.
        wanted = {
            "chrome/browser/profiles/profile.h": "//chrome/browser/profiles:profile",
            "chrome/browser/profiles/profile_keyed_service_factory.h": "//chrome/browser/profiles:profile",
            "chrome/browser/ui/browser_window/public/": "//chrome/browser/ui/browser_window",
            "chrome/browser/ui/tabs/tab_strip_model_observer.h": "//chrome/browser/ui/tabs:tab_strip_model_observer",
            "chrome/browser/ui/tabs/tab_strip_model.h": "//chrome/browser/ui/tabs:tab_strip",
            "chrome/browser/tab_list/": "//chrome/browser/tab_list",
            "components/tabs/public/": "//components/tabs:public",
            "components/keyed_service/core/": "//components/keyed_service/core",
            "components/prefs/": "//components/prefs",
            "components/aerie_tab_model/tab_model_prefs.h": "//components/aerie_tab_model:prefs",
            "components/aerie_tab_model/tab_model": "//components/aerie_tab_model",
        }
        gn = (SERVICE / "BUILD.gn").read_text()
        for path in SERVICE.glob("*.[ch]*"):
            if path.name.endswith("browsertest.cc"):
                continue
            for include in re.findall(r'#include "([^"]+)"', path.read_text()):
                for prefix, label in wanted.items():
                    if include.startswith(prefix):
                        self.assertIn(f'"{label}"', gn, f"{path.name} includes {include}")
        component_gn = (COMPONENT / "BUILD.gn").read_text()
        for path in COMPONENT.glob("*.cc"):
            if path.name.endswith("unittest.cc"):
                continue
            text = path.read_text()
            if "components/prefs/" in text:
                self.assertIn('"//components/prefs"', component_gn)
            self.assertNotIn("net/", " ".join(re.findall(r'#include "([^"]+)"', text)).replace("components/", ""))


class SeriesTest(unittest.TestCase):
    def test_the_two_patches_use_the_reserved_numbers_and_follow_the_last_hardening_patch(self):
        for name in (PREFS_PATCH, SERVICE_PATCH):
            self.assertTrue((REPO / "patches" / name).is_file(), name)
            number = int(name.split("/")[1][:4])
            self.assertTrue(10 <= number <= 19, name)
        self.assertEqual(SERIES.index(SERVICE_PATCH), SERIES.index(PREFS_PATCH) + 1)
        self.assertGreater(SERIES.index(PREFS_PATCH), SERIES.index("hardening/0020-restrict-fonts-to-an-allowlist.patch"))


class PrefNameTest(unittest.TestCase):
    """The names are in the C++ header and, as string literals, in the settings_private allowlist."""

    HEADER = (COMPONENT / "tab_model_prefs.h").read_text()
    PATCH = "\n".join(l.lstrip("+") for l in (REPO / "patches" / PREFS_PATCH).read_text().splitlines())

    def constant(self, name: str) -> str:
        return re.search(rf'{name}\[\] = "([^"]+)"', self.HEADER).group(1)

    def test_archive_after_is_a_string_in_the_allowlist(self):
        name = self.constant("kArchiveAfter")
        self.assertEqual(name, "aerie.tabs.archive_after")
        self.assertIn(f'(*s_allowlist)["{name}"] = settings_api::PrefType::kString;', self.PATCH)

    def test_sleep_after_is_a_number_in_the_allowlist(self):
        name = self.constant("kSleepAfterMinutes")
        self.assertEqual(name, "aerie.tabs.sleep_after_minutes")
        self.assertRegex(self.PATCH, re.escape(f'(*s_allowlist)["{name}"]') + r"\s*=\s*settings_api::PrefType::kNumber;")

    def test_the_defaults_are_twelve_hours_and_sixty_minutes(self):
        self.assertEqual(self.constant("kDefaultArchiveAfter"), "12h")
        self.assertRegex(self.HEADER, r"kDefaultSleepAfterMinutes = 60;")

    def test_the_favourites_cap_is_a_constant_and_not_a_pref(self):
        self.assertRegex((COMPONENT / "tab_model_types.h").read_text(), r"kFavoritesCap = 12;")
        self.assertNotIn("favorites_cap", self.HEADER + (COMPONENT / "tab_model_prefs.cc").read_text())

    def test_the_patch_registers_the_prefs_and_depends_on_their_target(self):
        self.assertIn("aerie_tab_model::RegisterProfilePrefs(registry);", self.PATCH)
        self.assertIn('"//components/aerie_tab_model:prefs"', self.PATCH)
        self.assertIn('#include "components/aerie_tab_model/tab_model_prefs.h"', self.PATCH)


class ServicePatchTest(unittest.TestCase):
    PATCH = (REPO / "patches" / SERVICE_PATCH).read_text()

    def test_the_factory_is_created_with_the_profile_and_its_target_is_a_dep(self):
        self.assertIn("aerie_tab_model::TabModelServiceFactory::GetInstance();", self.PATCH)
        self.assertIn('"//chrome/browser/aerie_tab_model"', self.PATCH)
        factory = code(SERVICE / "tab_model_service_factory.cc")
        self.assertRegex(factory, r"ServiceIsCreatedWithBrowserContext\(\) const \{\s*return true;")


class ModelShapeTest(unittest.TestCase):
    """What the issue's records and service list name."""

    TYPES = (COMPONENT / "tab_model_types.h").read_text()

    def test_the_record_fields_are_the_ones_in_the_issue(self):
        for field in ("workspace_id", "section", "parent", "position", "pinned_url", "manual_name", "last_active",
                      "sleeping", "archived_at", "kind", "rules", "behaviour", "colour", "theme", "mode", "icon"):
            self.assertRegex(self.TYPES, rf"\b{field}\b")
        for field in ("created", "modified", "change"):
            self.assertRegex(self.TYPES, rf"\b{field}\b")

    def test_the_default_workspace_is_personal_dusk_default_default(self):
        text = code(COMPONENT / "tab_model.cc")
        for line in ('workspace_.name = "Personal";', 'workspace_.colour = "dusk";',
                     'workspace_.theme = "default";', 'workspace_.mode = "default";'):
            self.assertIn(line, text)

    def test_the_theme_mode_and_section_names_are_persisted_values(self):
        self.assertEqual(re.findall(r'return "(\w+)";', code(COMPONENT / "tab_model_types.cc")
                                    .split("const char* SectionToString")[1].split("std::optional<Section>")[0]),
                         ["today", "pinned", "favorite"])

    def test_the_observer_has_the_four_events_for_tabs_and_folders(self):
        header = (COMPONENT / "tab_model_observer.h").read_text()
        for kind in ("Tab", "Folder"):
            for event in ("Added", "Changed", "Moved", "Removed"):
                self.assertIn(f"On{kind}{event}(", header)

    def test_time_and_ids_are_injected(self):
        header = (COMPONENT / "tab_model.h").read_text()
        self.assertIn("const base::Clock* clock", header)
        self.assertIn("IdGenerator", header)
        for path in COMPONENT.glob("*.cc"):
            if path.name.endswith("unittest.cc"):
                continue
            self.assertNotIn("base::Time::Now()", path.read_text(), path.name)
            self.assertNotIn("DefaultClock", path.read_text(), path.name)

    def test_the_state_file_is_in_the_profile_and_written_atomically(self):
        persistence = (COMPONENT / "tab_model_persistence.h").read_text()
        self.assertIn('kStateDirectory[] = "AerieTabModel"', persistence)
        self.assertIn('kStateFile[] = "state.json"', persistence)
        self.assertIn('kBadSuffix[] = ".bad"', persistence)
        self.assertIn("base::ImportantFileWriter", persistence)


class StartupTest(unittest.TestCase):
    """No disk or JSON work on the startup path."""

    def test_the_service_does_no_file_work_itself(self):
        text = code(SERVICE / "tab_model_service.cc")
        for call in ("ReadFile", "PathExists", "CreateDirectory", "JSONReader", "WriteJson", "ImportantFileWriter"):
            self.assertNotIn(call, text)
        self.assertIn("persistence_.Load();", text)

    def test_the_read_and_the_parse_run_on_the_io_runner(self):
        text = code(COMPONENT / "tab_model_persistence.cc")
        self.assertRegex(text, r"PostTaskAndReplyWithResult\(\s*FROM_HERE,\s*base::BindOnce\(&ReadStateFile")

    def test_nothing_is_saved_before_the_read_has_finished(self):
        text = code(COMPONENT / "tab_model_persistence.cc")
        self.assertRegex(text, r"if \(loaded_ && writer_\)")

    def test_an_off_the_record_profile_stays_in_memory(self):
        text = code(SERVICE / "tab_model_service.cc")
        self.assertRegex(text, r"if \(profile->IsOffTheRecord\(\)\) \{\s*return base::FilePath\(\);")
        self.assertIn("kOwnInstance", code(SERVICE / "tab_model_service_factory.cc"))


class NoNetworkNoStringsTest(unittest.TestCase):
    def test_nothing_in_the_tab_model_touches_the_network_or_a_grd(self):
        for directory in (COMPONENT, SERVICE):
            for path in directory.iterdir():
                if path.suffix in {".grd", ".grdp"}:
                    self.fail(f"{path.name}: this slice adds no user-visible string")
                if path.suffix in {".cc", ".h"} and not path.name.endswith("browsertest.cc"):
                    includes = re.findall(r'#include "([^"]+)"', path.read_text())
                    for include in includes:
                        # Pinned tabs (#180) compare a page's URL with the pinned URL: url/gurl.h parses a string, it
                        # makes no request.
                        if include == "url/gurl.h" and path.name.startswith("pinned_tabs"):
                            continue
                        # Auto-filing (#139) matches a page's URL against folder rules: same parse, no request.
                        if include == "url/gurl.h" and path.name.startswith(("rule_matcher", "folder_rules", "auto_file")):
                            continue
                        self.assertFalse(include.startswith(("net/", "services/network", "url/")), f"{path.name}: {include}")
                        # The sleep controller (#179) words its refusals from the sidebar's grd.
                        if path.name != "sleep_controller.cc":
                            self.assertNotIn("generated_resources", include)


class DocsTest(unittest.TestCase):
    DOC = (REPO / "docs" / "sidebar-design.md").read_text()

    def test_the_pinned_bit_decision_is_written(self):
        self.assertIn("## Tab Model", self.DOC)
        section = self.DOC.split("## Tab Model")[1].split("\n## ")[0]
        self.assertIn("PinnedTabService", section)
        self.assertIn("kPinnedTab", section)
        self.assertIn("do not carry the Chromium pinned bit", section)
        self.assertIn("TabGroupModel", section)

    def test_the_doc_names_what_this_slice_does_not_do(self):
        section = self.DOC.split("## Tab Model")[1].split("\n## ")[0]
        for text in ("#186", "does not match", "sync"):
            self.assertIn(text, section)


if __name__ == "__main__":
    unittest.main()
