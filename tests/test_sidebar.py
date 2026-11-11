"""The Aerie sidebar (issue #177): source checks for the parts the C++ unit tests cannot see, such as the pref names in
the settings_private allowlist, Helium's pref names, the series, the GN wiring and the written decisions.
The C++ itself is built and run by `./aerie test-cpp` (aerie_sidebar_unittests)."""

import re
import unittest
from pathlib import Path

from aerie_build import pipeline

REPO = Path(__file__).resolve().parent.parent
COMPONENT = REPO / "overlay" / "components" / "aerie_sidebar"
VIEWS = REPO / "overlay" / "chrome" / "browser" / "ui" / "views" / "aerie_sidebar"
HELIUM_LAYOUT = REPO / "helium-chromium" / "patches" / "helium" / "ui" / "layout"
SERIES = [l.strip() for l in (REPO / "patches" / "series").read_text().splitlines()
          if l.strip() and not l.startswith("#")]
PREFS_PATCH = "sidebar/0020-register-the-sidebar-prefs.patch"
HOLD_PATCH = "sidebar/0021-hold-helium-layout-to-the-sidebar.patch"
SETTINGS_PATCH = "sidebar/0022-show-an-empty-sidebar-section-in-settings.patch"
SAVE_PATCH = "sidebar/0023-move-save-page-to-ctrl-shift-s.patch"


def patch_text(name: str) -> str:
    return (REPO / "patches" / name).read_text()


def added(name: str) -> str:
    return "\n".join(l[1:] for l in patch_text(name).splitlines() if l.startswith("+") and not l.startswith("+++"))


def removed(name: str) -> str:
    return "\n".join(l[1:] for l in patch_text(name).splitlines() if l.startswith("-") and not l.startswith("---"))


def gn_sources(gn: Path) -> list[str]:
    return re.findall(r'"([A-Za-z0-9_./-]+\.(?:cc|h|inc))"', gn.read_text())


def constant(header: str, name: str) -> str:
    return re.search(rf'{name}\[\] = "([^"]+)"', header).group(1)


class SeriesTest(unittest.TestCase):
    def test_the_four_patches_are_in_the_series_in_order_after_the_tab_model(self):
        names = [PREFS_PATCH, HOLD_PATCH, SETTINGS_PATCH, SAVE_PATCH]
        for name in names:
            self.assertTrue((REPO / "patches" / name).is_file(), name)
        indexes = [SERIES.index(n) for n in names]
        self.assertEqual(indexes, sorted(indexes))
        self.assertGreater(indexes[0], SERIES.index("sidebar/0011-create-tab-model-service.patch"))

    def test_helium_layout_patches_are_not_dropped(self):
        drop = (REPO / "patches" / "helium-drop.txt").read_text()
        self.assertNotIn("ui/layout/", drop)


class PrefNameTest(unittest.TestCase):
    PREFS = (COMPONENT / "aerie_sidebar_prefs.h").read_text()
    LAYOUT = (COMPONENT / "aerie_sidebar_layout.h").read_text()

    def test_the_aerie_names_and_defaults(self):
        self.assertEqual(constant(self.PREFS, "kLayoutPref"), "aerie.sidebar.layout")
        self.assertEqual(constant(self.PREFS, "kWidthPref"), "aerie.sidebar.width")
        self.assertEqual(constant(self.PREFS, "kVisiblePref"), "aerie.sidebar.visible")
        self.assertEqual(constant(self.PREFS, "kLayoutSidebar"), "sidebar")
        self.assertEqual(constant(self.PREFS, "kLayoutHorizontal"), "horizontal")
        source = (COMPONENT / "aerie_sidebar_prefs.cc").read_text()
        self.assertIn("RegisterStringPref(kLayoutPref, kLayoutSidebar)", source)
        self.assertIn("kSizeSidebarWidth", source)
        self.assertRegex(source, r"RegisterBooleanPref\(kVisiblePref, true\)")

    def test_the_allowlist_has_the_width_and_visible_prefs_but_not_the_layout(self):
        text = added(PREFS_PATCH)
        self.assertNotIn(constant(self.PREFS, "kLayoutPref"), "".join(l for l in text.splitlines() if "s_allowlist" in l))
        for name, kind in (("kWidthPref", "kNumber"), ("kVisiblePref", "kBoolean")):
            literal = constant(self.PREFS, name)
            self.assertRegex(text, re.escape(f'(*s_allowlist)["{literal}"]') + r"\s*=\s*settings_api::PrefType::" + kind + ";")

    def test_the_patch_registers_the_prefs_and_depends_on_their_target(self):
        text = added(PREFS_PATCH)
        self.assertIn("aerie_sidebar::RegisterProfilePrefs(registry);", text)
        self.assertIn('"//components/aerie_sidebar:prefs"', text)
        self.assertIn('#include "components/aerie_sidebar/aerie_sidebar_prefs.h"', text)

    def test_helium_pref_names_and_the_vertical_value_are_the_ones_helium_defines(self):
        core = (HELIUM_LAYOUT / "core.patch").read_text()
        wiring = (HELIUM_LAYOUT / "zen-mode-wiring.patch").read_text()
        self.assertIn(f'kHeliumLayout[] = "{constant(self.LAYOUT, "kHeliumLayoutPref")}"', core)
        self.assertIn(f'kHeliumZenMode[] = "{constant(self.LAYOUT, "kHeliumZenModePref")}"', wiring)
        value = int(re.search(r"kHeliumLayoutVertical = (\d+);", self.LAYOUT).group(1))
        self.assertIn(f"kVertical = {value},", core)


class HoldTest(unittest.TestCase):
    def test_a_fresh_profile_registers_the_vertical_layout(self):
        self.assertIn("HeliumLayoutType::kVertical", added(HOLD_PATCH))
        self.assertIn("std::to_underlying(HeliumLayoutType::kClassic)", removed(HOLD_PATCH))

    def test_the_controller_enforces_at_start_and_on_every_change_in_one_registration(self):
        text = added(HOLD_PATCH)
        self.assertIn("aerie_sidebar::EnforceHeliumLayout(pref_service_);", text)
        # PrefChangeRegistrar allows one observer per pref, so the two jobs share one callback.
        after = "\n".join(l[1:] for l in patch_text(HOLD_PATCH).splitlines() if l[:1] in "+ " and not l.startswith("+++"))
        self.assertEqual(after.count("pref_change_registrar_.AddMultiple("), 1)
        for pref in ("prefs::kHeliumLayout", "prefs::kHeliumZenMode", "aerie_sidebar::kLayoutPref"):
            self.assertIn(pref, text)
        self.assertIn("self->NotifyStateChanged();", text)
        self.assertIn("NotifyStateChanged", removed(HOLD_PATCH))

    def test_the_includes_have_their_targets_in_the_gn_file(self):
        text = added(HOLD_PATCH)
        for header, label in (("components/aerie_sidebar/aerie_sidebar_layout.h", "//components/aerie_sidebar:layout"),
                              ("components/aerie_sidebar/aerie_sidebar_prefs.h", "//components/aerie_sidebar:prefs")):
            self.assertIn(f'#include "{header}"', text)
            self.assertIn(f'"{label}"', text)

    def test_settings_cannot_write_the_layout_or_zen_prefs(self):
        gone = removed(HOLD_PATCH)
        for pref in ("kHeliumLayout]", "kHeliumZenMode]", "kHeliumZenModeSidebarPinned]", "kHeliumZenModeTopChromePinned]"):
            self.assertIn(f"(*s_allowlist)[::prefs::{pref}", gone)

    def test_the_enforcement_is_off_for_the_horizontal_layout_and_writes_only_a_difference(self):
        source = (COMPONENT / "aerie_sidebar_layout.cc").read_text()
        self.assertRegex(source, r"if \(!IsSidebarLayout\(\*prefs\)\) \{\s*return;")
        self.assertIn("prefs->GetInteger(kHeliumLayoutPref) != kHeliumLayoutVertical", source)
        self.assertIn("if (prefs->GetBoolean(kHeliumZenModePref))", source)
        self.assertIn('GetString(kLayoutPref) != kLayoutHorizontal', source)


class SettingsTest(unittest.TestCase):
    GRDP = (VIEWS / "aerie_sidebar_strings.grdp").read_text()

    def test_the_page_is_an_empty_sidebar_section(self):
        html = added(SETTINGS_PATCH)
        self.assertIn('<settings-section page-title="$i18n{aerieSidebarPageTitle}">', html)
        gone = removed(SETTINGS_PATCH)
        for old in ("helium.browser.layout", "helium.browser.zen_mode", "BrowserLayout", "browserLayoutOptions_"):
            self.assertIn(old, gone)
        for kept in ("helium.browser.layout", "zen_mode", "controlled-radio-button"):
            self.assertNotIn(kept, html)

    def test_the_title_string_is_in_the_grdp_the_grd_and_the_string_provider(self):
        self.assertIn('name="IDS_SETTINGS_AERIE_SIDEBAR"', self.GRDP)
        self.assertIn("Sidebar", re.search(r"<message[^>]*>\s*([^<]+?)\s*</message>", self.GRDP).group(1))
        text = added(SETTINGS_PATCH)
        self.assertIn('{"aerieSidebarPageTitle", IDS_SETTINGS_AERIE_SIDEBAR},', text)
        self.assertIn('<part file="../browser/ui/views/aerie_sidebar/aerie_sidebar_strings.grdp" />', text)
        self.assertIn("chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_strings.grdp", text)

    def test_every_string_the_page_names_is_one_the_provider_gives(self):
        for name in re.findall(r"\$i18n\{(\w+)\}", added(SETTINGS_PATCH)):
            self.assertIn(f'"{name}"', added(SETTINGS_PATCH))


class SavePageTest(unittest.TestCase):
    def test_save_page_has_ctrl_shift_s(self):
        text = re.sub(r"\s+", " ", added(SAVE_PATCH))
        self.assertIn("{ui::VKEY_S, ui::EF_SHIFT_DOWN | ui::EF_PLATFORM_ACCELERATOR, IDC_SAVE_PAGE},", text)

    def test_the_decision_is_written_in_the_design_doc(self):
        doc = (REPO / "docs" / "sidebar-design.md").read_text()
        self.assertRegex(doc, r"\| Ctrl\+Shift\+S \| Save page")
        self.assertIn("Save page moves to Ctrl+Shift+S", doc)

    def test_the_doc_does_not_claim_the_view_exists(self):
        doc = (REPO / "docs" / "sidebar-design.md").read_text()
        self.assertIn("What is not built yet in the view", doc)
        # What the view has no code for is named as missing, not claimed.
        for missing in ("Favorites", "Pinned"):
            self.assertIn(missing, doc.split("What is not built yet in the view", 1)[1].split("\n", 1)[0])


class ViewWiringTest(unittest.TestCase):
    VIEW = REPO / "overlay" / "chrome" / "browser" / "ui" / "views" / "aerie_sidebar"

    def test_the_view_patches_are_in_the_series_after_the_layout_patches(self):
        series = (REPO / "patches" / "series").read_text().splitlines()
        view = ["sidebar/0024-add-the-sidebar-view-to-the-vertical-strip-region.patch",
                "sidebar/0025-keep-the-sidebar-width-in-its-pref-and-never-collapse.patch",
                "sidebar/0026-make-ctrl-s-show-or-hide-the-sidebar.patch",
                "sidebar/0027-slide-and-resize-the-sidebar.patch"]
        # Contiguous and in order: no patch of another series entry sits between two of them. Nothing here
        # pins them to the end of the list, so a later patch can be added after them.
        start = series.index(view[0])
        self.assertEqual(series[start:start + len(view)], view)
        before = [line for line in series[:start] if line.startswith("sidebar/00")]
        self.assertTrue(before, "the layout patches come first")
        self.assertEqual(before, sorted(before))
        self.assertTrue(all(line < view[0] for line in before))

    def test_every_source_of_the_view_is_in_its_gn_file(self):
        gn = (self.VIEW / "BUILD.gn").read_text()
        for source in self.VIEW.glob("*.cc"):
            self.assertIn(f'"{source.name}"', gn)
        for source in self.VIEW.glob("*.h"):
            self.assertIn(f'"{source.name}"', gn)

    def test_the_browser_tests_are_in_the_aerie_browser_test_binary(self):
        gn = (REPO / "overlay" / "chrome" / "test" / "aerie" / "BUILD.gn").read_text()
        self.assertIn('"//chrome/browser/ui/views/aerie_sidebar:browser_tests"', gn)

    def test_the_resize_and_toggle_use_the_headless_helpers(self):
        sidebar = REPO / "patches" / "sidebar"
        patch = (sidebar / "0026-make-ctrl-s-show-or-hide-the-sidebar.patch").read_text() + \
            (sidebar / "0027-slide-and-resize-the-sidebar.patch").read_text()
        for name in ("aerie_sidebar::ToggleVisible", "aerie_sidebar::ResizeByDrag", "aerie_sidebar::kKeyboardResizeStep"):
            self.assertIn(name, patch)


class BuildWiringTest(unittest.TestCase):
    def test_the_unit_test_binary_is_built_and_run_by_test_cpp(self):
        self.assertIn(("aerie_sidebar_unittests", ()), pipeline.TEST_BINARIES)

    def test_every_source_the_gn_file_names_exists_and_every_source_is_listed(self):
        listed = gn_sources(COMPONENT / "BUILD.gn")
        for name in listed:
            self.assertTrue((COMPONENT / name).is_file(), name)
        for path in COMPONENT.glob("*.[ch]*"):
            self.assertIn(path.name, listed)

    def test_every_included_chromium_header_has_its_target_in_deps(self):
        gn = (COMPONENT / "BUILD.gn").read_text()
        wanted = {
            "components/prefs/pref_service.h": "//components/prefs",
            "components/prefs/pref_registry_simple.h": "//components/prefs",
            "components/prefs/testing_pref_service.h": "//components/prefs:test_support",
            "components/aerie_design_tokens/aerie_tokens.h": "//components/aerie_design_tokens:tokens",
            "testing/gtest/include/gtest/gtest.h": "//testing/gtest",
        }
        for path in COMPONENT.glob("*.[ch]*"):
            for include in re.findall(r'#include "([^"]+)"', path.read_text()):
                if include in wanted:
                    self.assertIn(f'"{wanted[include]}"', gn, f"{path.name} includes {include}")

    def test_the_headless_component_includes_no_chrome_code(self):
        for path in COMPONENT.glob("*.[ch]*"):
            for include in re.findall(r'#include "([^"]+)"', path.read_text()):
                self.assertFalse(include.startswith(("chrome/", "content/", "ui/")), f"{path.name}: {include}")


class HeadlessPartsTest(unittest.TestCase):
    """The motion table, the Today list and the keys: the C++ tests run them; these checks keep their wiring and the
    token names honest."""

    MOTION = (COMPONENT / "aerie_sidebar_motion.cc").read_text()
    GN = (COMPONENT / "BUILD.gn").read_text()

    def test_show_and_hide_use_the_named_tokens(self):
        self.assertRegex(self.MOTION, r"ShowMotion[^}]*MotionDuration::kSlow, MotionEasing::kDecelerate", )
        self.assertRegex(self.MOTION, r"HideMotion[^}]*MotionDuration::kBase, MotionEasing::kAccelerate")
        self.assertIn("kMotionDistanceSidebarHideOffset", self.MOTION)

    def test_row_motion_uses_the_scale_and_slide_tokens(self):
        self.assertIn("kMotionScaleEnter", self.MOTION)
        self.assertIn("kMotionDistanceCloseSlide", self.MOTION)

    def test_the_tokens_the_motion_reads_have_the_design_values(self):
        import json
        tokens = json.loads((REPO / "overlay/components/aerie_design_tokens/tokens.json").read_text())
        self.assertEqual(tokens["motion-duration"]["slow"]["value"], 280)
        self.assertEqual(tokens["motion-duration"]["base"]["value"], 200)
        self.assertEqual(tokens["motion-distance"]["sidebar-hide-offset"]["value"], 24)
        self.assertEqual(tokens["motion-distance"]["close-slide"]["value"], 8)
        self.assertEqual(tokens["motion-scale"]["enter"]["value"], 0.96)

    def test_the_motion_names_no_property_but_transform_and_opacity_and_the_row_height(self):
        header = (COMPONENT / "aerie_sidebar_motion.h").read_text()
        fields = set(re.findall(r"bool (animate_\w+);", header))
        self.assertEqual(fields, {"animate_transform", "animate_opacity", "animate_scale", "animate_slide", "animate_height"})

    def test_the_new_targets_have_their_deps(self):
        for label in ('source_set("motion")', 'source_set("today_list")'):
            self.assertIn(label, self.GN)
        self.assertIn('"//components/aerie_tab_model"', self.GN)
        today = (COMPONENT / "aerie_sidebar_today_list.cc").read_text()
        self.assertIn('#include "components/aerie_tab_model/tab_model.h"', today)

    def test_the_docs_say_what_is_still_missing(self):
        doc = (REPO / "docs" / "sidebar-design.md").read_text()
        self.assertIn("Native sidebar view", doc)
        self.assertIn("`patches/sidebar/0024`", doc)
        self.assertIn("`patches/sidebar/0025`", doc)


if __name__ == "__main__":
    unittest.main()
