"""Render everything that is generated from the control table.

* docs/hardening.md: which controls upstream provided and which Aerie added.
* the settings page markup (hardening_page.html), shipped as a generated patch.
"""

from __future__ import annotations

import html
import json

PROVIDED_LABEL = {
    "chromium": "Chromium",
    "ungoogled": "ungoogled-chromium (through Helium)",
    "helium": "Helium",
    "aerie": "Aerie",
}
MARKUP_PATCH = "hardening/0004-hardening-settings-page-markup.patch"
PAGE_DIR = "chrome/browser/resources/settings/privacy_page/"
MARKUP_PATH = PAGE_DIR + "hardening_page.html"
OPTIONS_PATH = PAGE_DIR + "hardening_menu_options.ts"
PAGE_TITLE = "Security hardening"
PAGE_INTRO = ("Aerie turns these privacy and security protections on by default. "
              "Each row shows what the protection does, its default and what it can cost you.")

GENERATED_NOTE = ("Generated from tools/aerie_hardening/controls.json. Do not edit by hand. "
                  "Run: ./aerie hardening generate")


def _ui_label(c: dict) -> str:
    kind = c["ui"]["kind"]
    return {"dropdown": "Dropdown", "toggle": "Toggle", "readonly": "Read-only"}[kind]


def _control_section(c: dict) -> list[str]:
    out = [f"### {c['title']}", "", c["summary"], ""]
    out.append(f"- Default: {c['default']}")
    out.append(f"- Default provided by: {PROVIDED_LABEL[c['provided_by']]}")
    out.append("- Where the default is set:")
    out += [f"  - {s}" for s in c["default_set_in"]]
    out.append("- Upstream code:")
    out += [f"  - `{u}`" if " " not in u else f"  - {u}" for u in c["upstream"]]
    if c["pref"]:
        out.append(f"- Preference: `{c['pref']['name']}` (default `{json.dumps(c['pref']['default'])}`)")
    if c["aerie_patches"]:
        out.append("- Aerie patches:")
        out += [f"  - `patches/{p}`" for p in c["aerie_patches"]]
    out.append(f"- Settings row: {_ui_label(c)}")
    out.append(f"- Cost: {c['cost']}")
    out.append("- Bench checks: " + ", ".join(f"`{n}`" for n in c["bench_checks"]))
    out.append("")
    return out


def render_docs(table: dict) -> str:
    controls = table["controls"]
    lines = [
        "# Hardening defaults",
        "",
        f"<!-- {GENERATED_NOTE} -->",
        "",
        "Aerie ships strict but usable privacy and security defaults. Every one of them can be changed by the",
        "user in Settings, Privacy and security, Security hardening. No page can change them.",
        "This file says which controls the base browser already provided and which ones Aerie added.",
        "It is generated from `tools/aerie_hardening/controls.json`, the same table that drives the settings",
        "page and the Bench checks, so the three cannot drift apart.",
        "",
        "## Controls at a glance",
        "",
        "| Control | Default | Default provided by | Settings row | Bench checks |",
        "| --- | --- | --- | --- | --- |",
    ]
    for c in controls:
        checks = ", ".join(f"`{n}`" for n in c["bench_checks"])
        lines.append(f"| {c['title']} | {c['default']} | {PROVIDED_LABEL[c['provided_by']]} | {_ui_label(c)} | {checks} |")
    lines.append("")
    lines += ["## What upstream already provided", "",
              "Aerie only pins these with a Bench check and a settings row, so a Helium update cannot",
              "drop one without the build train noticing.", ""]
    for c in controls:
        if c["provided_by"] != "aerie":
            lines += _control_section(c)
    lines += ["## What Aerie added", ""]
    for c in controls:
        if c["provided_by"] == "aerie":
            lines += _control_section(c)
    lines += [
        "## HTTPS-only and local development",
        "",
        "HTTPS-only mode is strict by default. Chromium already skips the upgrade for `localhost`, the whole",
        "`127.0.0.0/8` block, `::1` and `*.localhost` (`net::IsLocalhost`), so a dev server on",
        "`http://localhost:<port>` loads with no warning page. The Bench check `hardening_localhost_dev`",
        "keeps that true.",
        "",
        "For any other http site, for example a dev server on the local network, the warning page has one",
        "Continue to site button. The choice is kept per host name for 15 days after the last visit",
        "(the `HTTP_ALLOWED` content setting). Settings, Privacy and security, Security hardening, lists",
        "every host that is allowed over http and has a Remove button for each one.",
        "",
        "## Extensions",
        "",
        "An extension cannot loosen these settings. The write permission of these `chrome.privacy` entries",
        "is removed, so the call fails with a permission error. Reading them and change events still work.",
        "",
        "| Extension API | Preference |",
        "| --- | --- |",
    ]
    for e in table["locked_from_extensions"]:
        lines.append(f"| `{e['extension_pref']}` | `{e['browser_pref']}` |")
    lines += [
        "",
        "Other extension routes, such as the content settings API for cookie exceptions, are not covered",
        "by this rule.",
        "",
        "## Not covered",
        "",
    ]
    for d in table["deferred"]:
        lines.append(f"- {d['title']}: {d['reason']}")
    lines += [
        "- Fonts, screen metrics and client hints are not randomized.",
        "- Helium keys its noise on the full origin, not on the registrable domain, so `a.example.com`",
        "  and `b.example.com` see different canvas values.",
        "",
        "## Change the table",
        "",
        "Edit `tools/aerie_hardening/controls.json`, then run:",
        "",
        "```",
        "./aerie hardening generate   # rewrites this file and the markup patch",
        "./aerie hardening check      # fails if either is stale or the table is invalid",
        "```",
        "",
        "The unit tests run the same check. Every row needs at least one Bench check, and every Bench check",
        "has a negative control that proves the check can fail. See `docs/bench.md`.",
    ]
    return "\n".join(lines) + "\n"


def _t(text: str) -> str:
    """Text node: the validator already forbids markup, quotes and ampersands."""
    return html.escape(text, quote=False)


def _a(text: str) -> str:
    return html.escape(text, quote=True)


def _control_block(c: dict) -> str:
    cid, ui = c["id"], c["ui"]
    wid = f"control-{cid}-setting"
    out = [f'    <div class="control" id="control-{cid}" data-control-id="{cid}">\n']
    if ui["kind"] == "toggle":
        out.append(f'      <settings-toggle-button id="{wid}" class="hr"\n'
                   f'          pref-key="{_a(ui["pref"])}"\n'
                   f'          label="{_a(c["title"])}"\n'
                   f'          sub-label="{_a(c["summary"])}">\n'
                   f'      </settings-toggle-button>\n')
    else:
        out.append('      <div class="cr-row hr">\n'
                   '        <div class="flex label-wrapper">\n'
                   f'          <div class="label">{_t(c["title"])}</div>\n'
                   f'          <div class="cr-secondary-text label">{_t(c["summary"])}</div>\n'
                   '        </div>\n')
        if ui["kind"] == "readonly":
            out.append(f'        <div class="cr-secondary-text control-state" id="{wid}">{_t(ui["state"])}</div>\n')
        else:
            bind = "${this.menuOptions_[" + repr(cid) + "]}"
            out.append(f'        <settings-dropdown-menu id="{wid}" label="{_a(c["title"])}"\n'
                       f'            pref-key="{_a(ui["pref"])}"\n'
                       f'            .menuOptions="{bind}">\n'
                       '        </settings-dropdown-menu>\n')
        out.append('      </div>\n')
    out.append('      <div class="control-meta cr-secondary-text">\n'
               f'        <div class="control-default">Default: {_t(c["default"])}</div>\n'
               f'        <div class="control-cost">Cost: {_t(c["cost"])}</div>\n'
               '      </div>\n'
               '    </div>\n')
    return "".join(out)


HTTP_EXCEPTIONS = """\
    <div class="cr-row hr">
      <div class="flex label-wrapper">
        <div class="label">Sites allowed over HTTP</div>
        <div class="cr-secondary-text label">
          Sites you chose to open over plain HTTP from the warning page. An entry ends 15 days after your last visit.
        </div>
      </div>
    </div>
    <div id="httpExceptions">
      ${this.httpExceptions_.length === 0 ? html`
        <div class="cr-row cr-secondary-text" id="httpExceptionsEmpty">No sites are allowed over HTTP.</div>
      ` : ''}
      ${this.httpExceptions_.map(host => html`
        <div class="cr-row http-exception" data-host="${host}">
          <div class="flex">${host}</div>
          <cr-button class="remove-http-exception" data-host="${host}"
              @click="${this.onRemoveHttpException_}">Remove</cr-button>
        </div>
      `)}
    </div>
"""


def render_markup(table: dict) -> str:
    out = [f"<!-- {GENERATED_NOTE} -->\n",
           f'  <settings-subpage page-title="{_a(PAGE_TITLE)}" route-path="${{this.routePath}}">\n',
           f'    <div class="cr-row first"><div class="cr-secondary-text" id="hardeningIntro">{_t(PAGE_INTRO)}</div></div>\n']
    for c in table["controls"]:
        out.append(_control_block(c))
    out.append(HTTP_EXCEPTIONS)
    out.append("  </settings-subpage>\n")
    return "".join(out)


def render_menu_options(table: dict) -> str:
    """hardening_menu_options.ts: the dropdown options, built once at load time."""
    lines = ["// Copyright 2026 The Aerie Authors",
             "// You can use, redistribute, and/or modify this source code under",
             "// the terms of the GPL-3.0 license that can be found in the LICENSE file.",
             "",
             f"// {GENERATED_NOTE}",
             "",
             "import type {DropdownMenuOptionList} from '../controls/settings_dropdown_menu.js';",
             "",
             "export const HARDENING_MENU_OPTIONS: {[controlId: string]: DropdownMenuOptionList} = {"]
    for c in table["controls"]:
        if c["ui"]["kind"] != "dropdown":
            continue
        lines.append(f"  {c['id']}: [")
        for o in c["ui"]["options"]:
            lines.append(f"    {{value: {json.dumps(o['value'])}, name: {json.dumps(o['label'])}}},")
        lines.append("  ],")
    lines.append("};")
    return "\n".join(lines) + "\n"


PATCH_HEADER = (
    "Aerie-Purpose: Add the Security hardening settings page markup and dropdown options, generated from the hardening control table so the page, the docs and the Bench cannot disagree.\n"
    "Aerie-Upstream-Area: chrome/browser/resources/settings/privacy_page (new files hardening_page.html and hardening_menu_options.ts)\n"
    "Aerie-Kind: hook\n"
    "\n"
    f"{GENERATED_NOTE}.\n"
    "\n"
)


def _new_file_diff(path: str, text: str) -> str:
    lines = text.splitlines()
    return (f"--- /dev/null\n+++ b/{path}\n@@ -0,0 +1,{len(lines)} @@\n"
            + "".join(f"+{line}\n" for line in lines))


def render_markup_patch(table: dict) -> str:
    return (PATCH_HEADER + _new_file_diff(OPTIONS_PATH, render_menu_options(table))
            + _new_file_diff(MARKUP_PATH, render_markup(table)))
