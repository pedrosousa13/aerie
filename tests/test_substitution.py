import hashlib
import shutil
import tempfile
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path
from unittest import mock

from aerie_build import substitution


def fake_fp(message) -> str:
    """Stands in for grit's fingerprint: a number derived from the visible text."""
    text = (message.text or "") + "".join(
        (ph.get("name") or "") + (ph.tail or "") for ph in message.findall("ph")
    )
    return str(int(hashlib.sha256(text.strip().encode()).hexdigest()[:12], 16))


GRD = """<?xml version="1.0" encoding="utf-8"?>
<grit>
  <release>
    <messages>
      <message name="IDS_PRODUCT_NAME" desc="Helium is in this description">
        Helium
      </message>
      <message name="IDS_ABOUT" desc="d">
        About Helium
      </message>
      <message name="IDS_PH" desc="d">
        Open <ph name="PRODUCT_NAME">$1<ex>Helium</ex></ph> now
      </message>
      <message name="IDS_COPYRIGHT" desc="d">
        Copyright The Helium Authors
      </message>
      <message name="IDS_SCHEME" desc="d">
        Visit helium://settings
      </message>
      <message name="IDS_WORD" desc="d">
        Heliumite is not a name
      </message>
      <message name="IDS_DOLLAR" desc="d">
        Costs &#36; for Helium
      </message>
      <message name="IDS_NOPE" desc="d">
        Nothing here
      </message>
    </messages>
  </release>
</grit>
"""


class ReplaceTextTest(unittest.TestCase):
    def check(self, before, after):
        self.assertEqual(substitution.replace_text(before)[0], after)

    def test_product_name_becomes_aerie(self):
        self.check("About Helium", "About Aerie")
        self.check("Helium's limit", "Aerie's limit")
        self.check("Helium.", "Aerie.")
        self.check("&amp;Helium", "&amp;Aerie")

    def test_scheme_authors_and_other_words_stay(self):
        self.check("helium://settings", "helium://settings")
        self.check("The Helium Authors", "The Helium Authors")
        self.check("Heliumite", "Heliumite")
        self.check("PreHelium", "PreHelium")

    def test_reports_whether_anything_matched(self):
        self.assertTrue(substitution.replace_text("Helium")[1])
        self.assertFalse(substitution.replace_text("Chromium")[1])
        self.assertFalse(substitution.replace_text("The Helium Authors")[1])

    def test_idempotent(self):
        once = substitution.replace_text("About Helium and Helium")[0]
        self.assertEqual(substitution.replace_text(once), (once, False))


class GrdTest(unittest.TestCase):
    def setUp(self):
        self.out, self.fp_map = substitution.substitute_grd_text(GRD, fake_fp)

    def test_message_text_is_rewritten(self):
        self.assertIn("About Aerie", self.out)
        self.assertIn('<ex>Aerie</ex>', self.out)
        self.assertNotIn("About Helium", self.out)

    def test_untouched_content_survives(self):
        self.assertIn("The Helium Authors", self.out)
        self.assertIn("helium://settings", self.out)
        self.assertIn("Heliumite is not a name", self.out)
        self.assertIn("Nothing here", self.out)

    def test_description_attribute_is_not_user_visible_and_stays(self):
        self.assertIn('desc="Helium is in this description"', self.out)

    def test_dollar_entity_is_preserved(self):
        self.assertIn("&#36;", self.out)

    def test_fingerprint_map_covers_changed_messages_only(self):
        old = fake_fp(_msg("About Helium"))
        new = fake_fp(_msg("About Aerie"))
        self.assertEqual(self.fp_map[old], new)
        self.assertNotIn(fake_fp(_msg("Nothing here")), self.fp_map)

    def test_file_without_helium_reports_no_change(self):
        out, fp_map = substitution.substitute_grd_text(GRD.replace("Helium", "Other"), fake_fp)
        self.assertIsNone(out)
        self.assertEqual(fp_map, {})


def _msg(text):
    import xml.etree.ElementTree as ET
    m = ET.Element("message")
    m.text = f"\n        {text}\n      "
    return m


XTB = """<?xml version="1.0" encoding="utf-8"?>
<translationbundle lang="de">
<translation id="111">Über Helium</translation>
<translation id="222">Ohne Namen</translation>
<translation id="333"><ph name="PRODUCT_NAME"/> Helium</translation>
</translationbundle>
"""


class XtbTest(unittest.TestCase):
    def test_text_and_ids_are_rewritten(self):
        out = substitution.substitute_xtb_text(XTB, {"111": "999", "222": "888"})
        self.assertIn('<translation id="999">Über Aerie</translation>', out)

    def test_id_is_remapped_even_without_the_name_in_the_translation(self):
        out = substitution.substitute_xtb_text(XTB, {"222": "888"})
        self.assertIn('id="888">Ohne Namen', out)

    def test_unrelated_file_is_left_alone(self):
        self.assertIsNone(substitution.substitute_xtb_text(XTB.replace("Helium", "X"), {"5": "6"}))

    def test_colliding_ids_are_bumped_so_the_build_does_not_break(self):
        out = substitution.substitute_xtb_text(XTB, {"111": "222"})
        ids = [part.split('"')[0] for part in out.split('translation id="')[1:]]
        self.assertEqual(len(ids), len(set(ids)))


class TreeTest(unittest.TestCase):
    def test_substitute_tree_rewrites_grd_then_xtb_and_skips_ignored_dirs(self):
        with tempfile.TemporaryDirectory() as tmp:
            tree = Path(tmp)
            grd = tree / "chrome" / "app" / "s.grd"
            xtb = tree / "chrome" / "app" / "resources" / "s_de.xtb"
            ignored = tree / "ash" / "x.grd"
            out_grd = tree / "out" / "Default" / "y.grd"
            for path, text in ((grd, GRD), (xtb, XTB), (ignored, GRD), (out_grd, GRD)):
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(text)
            fp_old = fake_fp(_msg("About Helium"))
            xtb.write_text(XTB.replace('id="111"', f'id="{fp_old}"'))
            stats = substitution.substitute_tree(tree, fake_fp, workers=1)
            self.assertIn("About Aerie", grd.read_text())
            self.assertEqual(ignored.read_text(), GRD)
            self.assertEqual(out_grd.read_text(), GRD)
            self.assertIn("Über Aerie", xtb.read_text())
            self.assertEqual(stats.grd_files, 1)
            self.assertEqual(stats.xtb_files, 1)

    def test_second_run_changes_nothing(self):
        with tempfile.TemporaryDirectory() as tmp:
            tree = Path(tmp)
            (tree / "s.grd").write_text(GRD)
            substitution.substitute_tree(tree, fake_fp, workers=1)
            first = (tree / "s.grd").read_text()
            stats = substitution.substitute_tree(tree, fake_fp, workers=1)
            self.assertEqual((tree / "s.grd").read_text(), first)
            self.assertEqual(stats.grd_files, 0)


def grd_with(messages: dict[str, str]) -> str:
    body = "".join(
        f'<message name="{name}" desc="d">\n        {text}\n      </message>\n' for name, text in messages.items()
    )
    return f'<?xml version="1.0" encoding="utf-8"?>\n<grit><release><messages>\n{body}</messages></release></grit>\n'


# Helium's own services and servers, taken from the real tree. None of it may
# start saying Aerie.
KEPT_AS_HELIUM = {
    "IDS_SETTINGS_HELIUM_SERVICES": "Helium services",
    "IDS_SETTINGS_HELIUM_SERVICES_TOGGLE": "Allow connecting to Helium services",
    "IDS_SETTINGS_HELIUM_SERVICES_DESCRIPTION": "Manage what Helium services are allowed in your browser",
    "IDS_SETTINGS_HELIUM_SERVICES_OVERRIDE": "Use your own instance of Helium services",
    "IDS_SETTINGS_HELIUM_SERVICES_OVERRIDE_ARIA_LABEL": "Custom origin URL for Helium services",
    "IDS_SETTINGS_HELIUM_SERVICES_SETUP_PENDING_TEXT": "Helium services are not available until setup is complete",
    "IDS_SETTINGS_HELIUM_SCHEMA_NOTICE_TITLE": "Updates to Helium services:",
    "IDS_SETTINGS_HELIUM_SERVICES_EXT_PROXY_TOGGLE_DESCRIPTION": "Helium will proxy extension downloads and updates",
    "IDS_SETTINGS_HELIUM_SERVICES_UBO_DESCRIPTION": "Helium will fetch fresh filter lists. All requests to lists are proxied",
    "IDS_SETTINGS_HELIUM_SERVICES_UPDATE_DESCRIPTION": "Please check for Helium package updates at least every week.",
    "IDS_SETTINGS_LANGUAGES_DICTIONARY_DOWNLOAD_FAILED_HELP_HELIUM": "block downloads from Helium servers",
    "IDS_HELIUM_ONBOARDING_SERVICES_TITLE": "Helium services",
    "IDS_HELIUM_ONBOARDING_SERVICES_AUTOUPDATES_DESC_LINUX": "Helium will automatically check for component updates",
    "IDS_HELIUM_ONBOARDING_SERVICES_EXTENSIONS_DESC": "Helium will proxy extension downloads",
    "IDS_HELIUM_ONBOARDING_WELCOME_FOOTER": "If you use defaults, Helium services will be automatically enabled.",
    "IDS_HELIUM_ONBOARDING_PARTNER_TITLE": "Helium Partner",
    "IDS_HELIUM_ONBOARDING_PARTNER_TOOLTIP": "Paying for this service will support the development of Helium.",
    "IDS_HELIUM_SERVICES_SCHEMA_MENU_ITEM": "Review Helium services updates",
    "IDS_APPMENU_TOOLTIP_HELIUM_SERVICES_UPDATE": "Helium services have been updated.",
    "IDS_COMPONENTS_SVC_STATUS_ERROR_HELIUM_SERVICES": "See Helium services in settings.",
    "IDS_DOWNLOAD_BUBBLE_INTERRUPTED_SUBPAGE_SUMMARY_HELIUM_SERVICES_DISABLED": "Enable this in Helium services settings",
}

# The names users see: window title, About page, app menu, version page.
BECOMES_AERIE = {
    "IDS_PRODUCT_NAME": "Helium",
    "IDS_SHORT_PRODUCT_NAME": "Helium",
    "IDS_BROWSER_WINDOW_TITLE_FORMAT": 'Helium - <ph name="PAGE_TITLE">$1<ex>Google</ex></ph>',
    "IDS_SETTINGS_ABOUT_PROGRAM": "About Helium",
    "IDS_ABOUT_CHROME": "About &amp;Helium",
    "IDS_SETTINGS_ABOUT_PAGE_BROWSER_VERSION": "Helium version",
    "IDS_SETTINGS_DEFAULT_BROWSER_DEFAULT": "Helium is your default browser",
    "IDS_HELIUM_ONBOARDING_WELCOME_GREETING": "Meet Helium",
    # About page update status, app menu and update wording now say Aerie too.
    "IDS_SETTINGS_UPGRADE_UP_TO_DATE": "Helium is up to date",
    "IDS_SETTINGS_UPGRADE_UPDATING": "Updating Helium",
    "IDS_SETTINGS_UPGRADE_SUCCESSFUL_RELAUNCH": "Nearly up to date! Relaunch Helium to finish updating.",
    "IDS_APPMENU_TOOLTIP_UPDATE_AVAILABLE": "Customize and control Helium. Update is available.",
    "IDS_UPGRADE_BUBBLE_TITLE": "Can't update Helium",
    "IDS_RELAUNCH_TO_UPDATE": "Relaunch to update &amp;Helium",
    "IDS_UPDATE_RECOMMENDED": "A new update for Helium is available",
    "IDS_UPDATE_RECOMMENDED_ALT": "Not used in Helium. Placeholder to keep resource maps in sync.",
    "IDS_ACC_OMNIBOX_PEDAL_UPDATE_CREDIT_CARD": "Manage payment methods in Helium settings",
    "IDS_AUTOFILL_AI_UPDATE_REAUTH_REQUIREMENT": "Helium wants to verify it's you before you change settings",
    "IDS_SYNC_UPGRADE_CLIENT": "Update Helium to start sync",
    "IDS_SETTINGS_SAFETY_HUB_VERSION_CARD_HEADER_UPDATED": "Helium is up to date",
    "IDS_HELIUM_CRASH_REPORTING_DIALOG_DESCRIPTION": "Helium closed unexpectedly.",
    "IDS_HELIUM_CRASH_REPORTING_DIALOG_PRIVACY": "sensitive data present in Helium's memory",
    "IDS_SETTINGS_HELIUM_CRASH_REPORTING_DESCRIPTION": "Choose whether Helium can send crash reports",
    "IDS_EXTENSIONS_MODERN_WEB_GUIDANCE_PROMO": "Install the Helium Extensions skill",
    "IDS_HELIUM_ONBOARDING_SEARCH_CATEGORIES_CUSTOM": "Its privacy wasn't verified by Helium.",
}


class DenylistTest(unittest.TestCase):
    def run_pass(self, messages):
        out, _ = substitution.substitute_grd_text(grd_with(messages), fake_fp)
        return out or grd_with(messages)

    def test_services_servers_and_partner_messages_keep_helium(self):
        out = self.run_pass(KEPT_AS_HELIUM)
        self.assertNotIn("Aerie", out)
        for text in KEPT_AS_HELIUM.values():
            self.assertIn(text, out)

    def test_title_about_menu_version_and_update_messages_say_aerie(self):
        out = self.run_pass(BECOMES_AERIE)
        self.assertNotIn("Helium", out)
        self.assertEqual(out.count("Aerie"), len(BECOMES_AERIE))

    def test_nothing_is_kept_just_because_update_is_in_its_name(self):
        for pattern, _ in substitution.KEEP_MESSAGES:
            self.assertNotRegex(pattern.pattern, r'UPDATE|UPGRADE|CRASH')

    def test_every_denylist_entry_has_a_reason(self):
        self.assertTrue(substitution.KEEP_MESSAGES)
        for pattern, reason in substitution.KEEP_MESSAGES:
            self.assertGreater(len(reason), 10, pattern.pattern)

    def test_denylist_is_part_of_the_rules_digest(self):
        before = substitution.rules_digest()
        with mock.patch.object(substitution, "KEEP_MESSAGES", substitution.KEEP_MESSAGES[:-1]):
            self.assertNotEqual(substitution.rules_digest(), before)

    def test_denied_message_does_not_produce_a_fingerprint_entry(self):
        _, fp_map = substitution.substitute_grd_text(grd_with(KEPT_AS_HELIUM), fake_fp)
        self.assertEqual(fp_map, {})


FIXTURE = Path(__file__).parent / "fixtures" / "helium_messages.grdp"

# Every message in the real Helium tree (0.21.1.1) that keeps its Helium wording.
EXPECTED_KEPT = sorted("""
IDS_APPMENU_TOOLTIP_HELIUM_SERVICES_UPDATE IDS_COMPONENTS_SVC_STATUS_ERROR_HELIUM_SERVICES
IDS_DOWNLOAD_BUBBLE_INTERRUPTED_SUBPAGE_SUMMARY_HELIUM_SERVICES_DISABLED
IDS_HELIUM_ONBOARDING_PARTNER_TITLE IDS_HELIUM_ONBOARDING_PARTNER_TOOLTIP
IDS_HELIUM_ONBOARDING_SERVICES_AUTOUPDATES_DESC IDS_HELIUM_ONBOARDING_SERVICES_AUTOUPDATES_DESC_LINUX
IDS_HELIUM_ONBOARDING_SERVICES_BANGS_DESC IDS_HELIUM_ONBOARDING_SERVICES_CONNECTION_DESC
IDS_HELIUM_ONBOARDING_SERVICES_CONNECTION_TITLE IDS_HELIUM_ONBOARDING_SERVICES_EXTENSIONS_DESC
IDS_HELIUM_ONBOARDING_SERVICES_INSTANCE_DESC IDS_HELIUM_ONBOARDING_SERVICES_INSTANCE_TITLE
IDS_HELIUM_ONBOARDING_SERVICES_SPELLCHECK_DESC IDS_HELIUM_ONBOARDING_SERVICES_TITLE
IDS_HELIUM_ONBOARDING_SERVICES_UBLOCK_DESC IDS_HELIUM_ONBOARDING_WELCOME_FOOTER
IDS_HELIUM_SERVICES_SCHEMA_MENU_ITEM IDS_SETTINGS_HELIUM_SCHEMA_NOTICE_TITLE
IDS_SETTINGS_HELIUM_SERVICES IDS_SETTINGS_HELIUM_SERVICES_BANGS_TOGGLE_DESCRIPTION
IDS_SETTINGS_HELIUM_SERVICES_DESCRIPTION IDS_SETTINGS_HELIUM_SERVICES_EXT_PROXY_TOGGLE_DESCRIPTION
IDS_SETTINGS_HELIUM_SERVICES_OVERRIDE IDS_SETTINGS_HELIUM_SERVICES_OVERRIDE_ARIA_LABEL
IDS_SETTINGS_HELIUM_SERVICES_OVERRIDE_DESCRIPTION IDS_SETTINGS_HELIUM_SERVICES_SETUP_PENDING_TEXT
IDS_SETTINGS_HELIUM_SERVICES_SPELLCHECK_TOGGLE_DESCRIPTION IDS_SETTINGS_HELIUM_SERVICES_TOGGLE
IDS_SETTINGS_HELIUM_SERVICES_TOGGLE_DESCRIPTION IDS_SETTINGS_HELIUM_SERVICES_UBO_DESCRIPTION
IDS_SETTINGS_HELIUM_SERVICES_UPDATE_DESCRIPTION
IDS_SETTINGS_LANGUAGES_DICTIONARY_DOWNLOAD_FAILED_HELP_HELIUM
""".split())


class RealMessageSetTest(unittest.TestCase):
    """Runs the pass over 270 real messages (a fixture cut from the Helium tree)."""

    def setUp(self):
        self.text = FIXTURE.read_text(encoding="utf-8")
        self.out, _ = substitution.substitute_grd_text(self.text, fake_fp)

    def messages(self, text):
        root = ET.fromstring(text)
        return {m.get("name"): "".join(m.itertext()) for m in root.iter("message")}

    def test_the_kept_messages_are_exactly_the_expected_list(self):
        kept = substitution.kept_messages(self.text)
        print(f"\nkept {len(kept)} message(s) as Helium:", *kept, sep="\n  ")
        self.assertEqual(kept, EXPECTED_KEPT)

    def test_kept_messages_still_say_helium_and_never_aerie(self):
        for name, text in self.messages(self.out).items():
            if name in EXPECTED_KEPT:
                self.assertIn("Helium", text, name)
                self.assertNotIn("Aerie", text, name)

    def test_every_other_message_says_aerie_not_helium(self):
        after = self.messages(self.out)
        for name, text in after.items():
            if name not in EXPECTED_KEPT:
                self.assertNotRegex(text.replace("The Helium Authors", ""), r"(?<!\w)Helium(?!\w)", name)

    def test_about_pedal_autofill_and_update_strings_say_aerie(self):
        after = self.messages(self.out)
        for name in ("IDS_SETTINGS_UPGRADE_UP_TO_DATE", "IDS_SETTINGS_UPGRADE_UPDATING",
                     "IDS_ACC_OMNIBOX_PEDAL_UPDATE_CREDIT_CARD", "IDS_AUTOFILL_AI_UPDATE_REAUTH_REQUIREMENT",
                     "IDS_HELIUM_CRASH_REPORTING_DIALOG_DESCRIPTION", "IDS_HELIUM_CRASH_REPORTING_DIALOG_PRIVACY"):
            self.assertIn(name, after)
            self.assertIn("Aerie", after[name], name)

    def test_tree_run_reports_the_kept_names(self):
        with tempfile.TemporaryDirectory() as tmp:
            tree = Path(tmp)
            (tree / "s.grdp").write_text(self.text, encoding="utf-8")
            stats = substitution.substitute_tree(tree, fake_fp, workers=1)
            self.assertEqual(sorted(stats.kept), EXPECTED_KEPT)


class InterruptedRunTest(unittest.TestCase):
    """A run that dies part way must be repairable by running again."""

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.tree = Path(self._tmp.name)
        self.grds = []
        for i in range(3):
            path = self.tree / "chrome" / "app" / f"s{i}.grd"
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(grd_with({f"IDS_ONE_{i}": f"About Helium {i}", f"IDS_TWO_{i}": f"Open Helium {i}"}))
            self.grds.append(path)
        self.xtb = self.tree / "chrome" / "app" / "resources" / "s_de.xtb"
        self.xtb.parent.mkdir(parents=True)
        ids = [fake_fp(_msg(f"About Helium {i}")) for i in range(3)] + [fake_fp(_msg(f"Open Helium {i}")) for i in range(3)]
        self.xtb.write_text(
            '<?xml version="1.0" encoding="utf-8"?>\n<translationbundle lang="de">\n'
            + "".join(f'<translation id="{id_}">Über Helium {n}</translation>\n' for n, id_ in enumerate(ids))
            + "</translationbundle>\n"
        )
        self.state = self.tree / ".fingerprints.json"

    def tearDown(self):
        self._tmp.cleanup()

    def run_tree(self):
        return substitution.substitute_tree(self.tree, fake_fp, workers=1, state_path=self.state)

    def snapshot(self):
        return {str(p.relative_to(self.tree)): p.read_text() for p in [*self.grds, self.xtb]}

    def reference(self):
        """What an uninterrupted run produces from the same starting files."""
        saved = self.snapshot()
        self.run_tree()
        done = self.snapshot()
        for rel, text in saved.items():
            (self.tree / rel).write_text(text)
        self.state.unlink(missing_ok=True)
        return done

    def crash_after(self, writes: int):
        real = substitution._write_atomic
        count = {"n": 0}

        def flaky(path, text):
            if count["n"] >= writes:
                raise KeyboardInterrupt("simulated interrupt")
            count["n"] += 1
            real(path, text)

        return mock.patch.object(substitution, "_write_atomic", flaky)

    def test_interrupt_while_rewriting_grd_files(self):
        expected = self.reference()
        # Write 1 is the saved fingerprint map; write 2 is the first grd file.
        with self.crash_after(2), self.assertRaises(KeyboardInterrupt):
            self.run_tree()
        done = [g for g in self.grds if "Aerie" in g.read_text()]
        self.assertEqual(len(done), 1)
        self.run_tree()
        self.assertEqual(self.snapshot(), expected)

    def test_interrupt_between_the_grd_and_xtb_stages(self):
        expected = self.reference()
        # Writes: 1 saved map + 3 grd files. The 5th write (first xtb) is the one that dies.
        with self.crash_after(4), self.assertRaises(KeyboardInterrupt):
            self.run_tree()
        self.assertTrue(all("Aerie" in g.read_text() for g in self.grds))
        self.assertNotIn("Aerie", self.xtb.read_text())
        self.run_tree()
        self.assertEqual(self.snapshot(), expected)

    def test_the_fingerprint_map_is_saved_before_any_file_is_rewritten(self):
        with self.crash_after(1), self.assertRaises(KeyboardInterrupt):  # dies on the first grd write
            self.run_tree()
        self.assertTrue(self.state.exists())
        self.assertTrue(all("Aerie" not in g.read_text() for g in self.grds))

    def test_no_temp_files_are_left_behind_and_files_are_never_half_written(self):
        with self.crash_after(2), self.assertRaises(KeyboardInterrupt):
            self.run_tree()
        self.assertEqual([p.name for p in self.tree.rglob("*.aerie-tmp")], [])
        for grd in self.grds:
            ET.parse(grd)  # still well-formed, whether rewritten or not

    def test_second_complete_run_changes_nothing(self):
        self.run_tree()
        first = self.snapshot()
        self.run_tree()
        self.assertEqual(self.snapshot(), first)

    def test_atomic_write_replaces_the_file(self):
        target = self.tree / "x.txt"
        target.write_text("old")
        substitution._write_atomic(target, "new")
        self.assertEqual(target.read_text(), "new")
        self.assertEqual(list(self.tree.glob("x.txt.*")), [])


if __name__ == "__main__":
    unittest.main()
