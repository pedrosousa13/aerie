import json
import shutil
import tempfile
import unittest
from pathlib import Path

from aerie_hardening import model, render
from aerie_hardening import __main__ as cli

REPO = Path(__file__).resolve().parent.parent
TABLE = model.load()


class DocsTest(unittest.TestCase):
    def setUp(self):
        self.doc = render.render_docs(TABLE)

    def test_docs_file_is_in_sync_with_the_table(self):
        on_disk = (REPO / "docs" / "hardening.md").read_text(encoding="utf-8")
        self.assertEqual(on_disk, self.doc, "docs/hardening.md is stale; run: ./aerie hardening generate")

    def test_every_control_is_listed_with_default_owner_and_source(self):
        for c in TABLE["controls"]:
            self.assertIn(c["title"], self.doc)
            self.assertIn(c["default"], self.doc)
            self.assertIn(c["cost"], self.doc)
            for src in c["default_set_in"]:
                self.assertIn(src, self.doc)
            for check in c["bench_checks"]:
                self.assertIn(f"`{check}`", self.doc)
            self.assertIn(render.PROVIDED_LABEL[c["provided_by"]], self.doc)

    def test_provided_and_added_sections_split_by_owner(self):
        provided, added = self.doc.split("## What Aerie added")
        self.assertIn("## What upstream already provided", provided)
        for c in TABLE["controls"]:
            section = added if c["provided_by"] == "aerie" else provided
            other = provided if c["provided_by"] == "aerie" else added
            self.assertIn(f"### {c['title']}", section)
            self.assertNotIn(f"### {c['title']}", other)

    def test_deferred_and_extension_lock_are_documented(self):
        for d in TABLE["deferred"]:
            self.assertIn(d["title"], self.doc)
        for e in TABLE["locked_from_extensions"]:
            self.assertIn(e["extension_pref"], self.doc)

    def test_render_is_deterministic_and_ends_with_newline(self):
        self.assertEqual(self.doc, render.render_docs(TABLE))
        self.assertTrue(self.doc.endswith("\n"))
        self.assertNotIn("\n\n\n", self.doc)


class MarkupTest(unittest.TestCase):
    def setUp(self):
        self.html = render.render_markup(TABLE)

    def test_one_row_per_control_with_default_and_cost(self):
        for c in TABLE["controls"]:
            self.assertIn(f'id="control-{c["id"]}"', self.html)
            self.assertIn(f'data-control-id="{c["id"]}"', self.html)
            self.assertIn(f"Default: {c['default']}", self.html)
            self.assertIn(f"Cost: {c['cost']}", self.html)

    def test_widgets_bind_to_the_table_prefs(self):
        for c in TABLE["controls"]:
            ui = c["ui"]
            if ui["kind"] == "readonly":
                self.assertIn(ui["state"], self.html)
            else:
                self.assertIn(f'pref-key="{ui["pref"]}"', self.html)
        self.assertIn("settings-toggle-button", self.html)
        self.assertIn("settings-dropdown-menu", self.html)

    def test_dropdowns_bind_a_constant_that_is_built_once(self):
        ts = render.render_menu_options(TABLE)
        for c in TABLE["controls"]:
            if c["ui"]["kind"] != "dropdown":
                continue
            self.assertIn(f".menuOptions=\"${{this.menuOptions_['{c['id']}']}}\"", self.html)
            self.assertIn(f"  {c['id']}: [", ts)
            for o in c["ui"]["options"]:
                self.assertIn(f"{{value: {json.dumps(o['value'])}, name: {json.dumps(o['label'])}}},", ts)
        self.assertNotIn(".menuOptions=\"${[", self.html)  # no array literal rebuilt on every render

    def test_menu_options_file_is_valid_typescript_shape(self):
        ts = render.render_menu_options(TABLE)
        self.assertIn("export const HARDENING_MENU_OPTIONS", ts)
        self.assertTrue(ts.endswith("};\n"))

    def test_http_exceptions_section_and_no_i18n_syntax(self):
        self.assertIn('id="httpExceptions"', self.html)
        self.assertIn("onRemoveHttpExceptionClick_", self.html)
        self.assertNotIn("$i18n", self.html)

    def test_patch_for_markup_is_in_sync_and_has_a_header(self):
        path = REPO / "patches" / render.MARKUP_PATCH
        self.assertEqual(path.read_text(encoding="utf-8"), render.render_markup_patch(TABLE),
                         "markup patch is stale; run: ./aerie hardening generate")
        text = path.read_text(encoding="utf-8")
        self.assertTrue(text.startswith("Aerie-Purpose: "))
        self.assertIn("Aerie-Kind: hook", text)
        self.assertIn("Do not edit by hand", text)
        self.assertIn("--- /dev/null\n+++ b/" + render.MARKUP_PATH, text)
        self.assertIn("--- /dev/null\n+++ b/" + render.OPTIONS_PATH, text)

    def test_patch_line_counts_match_the_hunk_headers(self):
        patch = render.render_markup_patch(TABLE)
        body = patch.split("--- /dev/null\n")[1:]
        self.assertEqual(len(body), 2)
        for chunk in body:
            _, header, *lines = chunk.splitlines()
            self.assertEqual(int(header.split("+1,")[1].split(" ")[0]), len(lines))
            self.assertTrue(all(l.startswith("+") for l in lines))


def quiet(argv):
    import contextlib
    import io
    with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
        return cli.main(argv)


class CliTest(unittest.TestCase):
    def test_check_passes_in_the_repo(self):
        self.assertEqual(quiet(["check"]), 0)

    def test_check_fails_on_stale_files_and_generate_repairs(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            (root / "docs").mkdir()
            (root / "patches").mkdir()
            self.assertEqual(quiet(["check", "--repo", str(root)]), 1)
            self.assertEqual(quiet(["generate", "--repo", str(root)]), 0)
            self.assertEqual(quiet(["check", "--repo", str(root)]), 0)
            (root / "docs" / "hardening.md").write_text("stale")
            self.assertEqual(quiet(["check", "--repo", str(root)]), 1)


if __name__ == "__main__":
    unittest.main()
