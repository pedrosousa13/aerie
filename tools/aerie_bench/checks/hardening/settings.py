"""The settings page lists every control with its default, and the live prefs match the table."""

from __future__ import annotations

import json
import time

from aerie_hardening import model

from ...session import Session
from .. import register
from .common import DEEP_JS, HardeningCheck, Verdict, page_value, site
from .cookies import cookie_verdict, probe_cookies
from .https import SETTINGS_URL

SETTINGS_ROOT = "chrome://settings/"


def table() -> dict:
    return model.load()


def pref_controls(tbl: dict) -> list[dict]:
    return [c for c in tbl["controls"] if c["pref"]]


def read_prefs(session: Session, names: list[str]) -> dict:
    """Read prefs through chrome.settingsPrivate on the settings page."""
    nav = session.navigate_url(SETTINGS_ROOT)
    if nav["error"]:
        return {"values": {}, "missing": list(names), "error": nav["error"]}
    session.wait_for("typeof chrome !== 'undefined' && !!chrome.settingsPrivate", timeout=20)
    js = ("""(async (names) => {
      const values = {}, missing = [];
      for (const n of names) {
        const pref = await new Promise((res) => chrome.settingsPrivate.getPref(n, (p) => res(p)));
        if (chrome.runtime.lastError || !pref) { missing.push(n); } else { values[n] = pref.value; }
      }
      return {values, missing};
    })(%s)""" % json.dumps(names))
    return session.evaluate(js, await_promise=True)


def pref_mismatches(tbl: dict, probe: dict) -> list[str]:
    """Prefs that are missing or differ from the table default."""
    out = [f"{n}: not readable" for n in probe.get("missing", [])]
    for c in pref_controls(tbl):
        name, default = c["pref"]["name"], c["pref"]["default"]
        if name in probe.get("values", {}) and probe["values"][name] != default:
            out.append(f"{name}: is {probe['values'][name]!r}, table says {default!r}")
    return out


@register
class PrefDefaults(HardeningCheck):
    name = "hardening_pref_defaults"
    control = "all"
    negative_control = ("A second browser whose profile seeds a different WebRTC policy must be reported as "
                        "different from the table, so the comparison can see a mismatch.")

    def observe(self, session: Session) -> dict:
        names = [c["pref"]["name"] for c in pref_controls(table())]
        main = read_prefs(session, names)
        with session.variant((), {"webrtc": {"ip_handling_policy": "default"}}) as v:
            control = read_prefs(v, names)
        return {"main": main, "control": control}

    @staticmethod
    def judge(obs: dict) -> Verdict:
        tbl = table()
        bad = pref_mismatches(tbl, obs["main"])
        if bad:
            return "fail", "the live defaults differ from controls.json: " + "; ".join(bad)
        if not any(m.startswith("webrtc.ip_handling_policy") for m in pref_mismatches(tbl, obs["control"])):
            return "fail", "negative control failed: a changed WebRTC policy was not reported as a mismatch"
        return "pass", f"{len(pref_controls(tbl))} live pref defaults equal the table"


LIST_ROWS_JS = """((ids) => {
  const rows = {};
  for (const id of ids) {
    const row = deepQuery(document, '#control-' + id);
    if (!row) { rows[id] = {present: false}; continue; }
    const text = (sel) => { const e = row.querySelector(sel); return e ? e.textContent.trim() : null; };
    const setting = row.querySelector('#control-' + id + '-setting');
    let live = null;
    if (setting && setting.tagName === 'SETTINGS-DROPDOWN-MENU') {
      const sel = setting.shadowRoot && setting.shadowRoot.querySelector('select');
      live = sel && sel.selectedOptions[0] ? sel.selectedOptions[0].textContent.trim() : null;
    } else if (setting && setting.tagName === 'SETTINGS-TOGGLE-BUTTON') {
      live = setting.checked ? 'on' : 'off';
    }
    rows[id] = {present: true, default_text: text('.control-default'), cost_text: text('.control-cost'),
                state_text: setting && setting.classList.contains('control-state') ? setting.textContent.trim() : null,
                live: live, route: setting ? setting.getAttribute('data-route') : null};
  }
  return rows;
})(%s)"""


def read_rows(session: Session, ids: list[str], live_ids: list[str]) -> dict:
    nav = session.navigate_url(SETTINGS_URL)
    if nav["error"]:
        return {"rows": {}, "error": nav["error"]}
    session.wait_for(DEEP_JS + f"!!deepQuery(document, '#control-{ids[0]}')", timeout=20)
    deadline = time.monotonic() + 10
    while True:  # dropdowns fill in their selection a moment after they render
        rows = session.evaluate(DEEP_JS + LIST_ROWS_JS % json.dumps(ids))
        if all(rows.get(i, {}).get("live") for i in live_ids) or time.monotonic() > deadline:
            break
        time.sleep(0.3)
    return {"rows": rows}


def default_label(c: dict) -> str | None:
    ui = c["ui"]
    if ui["kind"] == "dropdown":
        return next(o["label"] for o in ui["options"] if o["value"] == ui["default_value"])
    if ui["kind"] == "toggle":
        return "on" if c["pref"]["default"] else "off"
    return None


@register
class SettingsPage(HardeningCheck):
    name = "hardening_settings_page"
    control = "all"
    negative_control = "A control id that is not in the table is looked up the same way and must be absent."

    def observe(self, session: Session) -> dict:
        controls = table()["controls"]
        ids = [c["id"] for c in controls]
        live_ids = [c["id"] for c in controls if c["ui"]["kind"] in ("dropdown", "toggle")]
        got = read_rows(session, ids + ["bogus_control"], live_ids)
        rows = got.get("rows", {})
        bogus = rows.pop("bogus_control", {"present": False})
        return {"rows": rows, "bogus_present": bool(bogus.get("present")), "error": got.get("error")}

    @staticmethod
    def judge(obs: dict) -> Verdict:
        if obs.get("error") or not obs["rows"]:
            return "fail", f"the Security hardening page did not load ({obs.get('error')})"
        if obs["bogus_present"]:
            return "fail", "negative control failed: a control id that is not in the table was found on the page"
        for c in table()["controls"]:
            row = obs["rows"].get(c["id"], {"present": False})
            if not row.get("present"):
                return "fail", f"no row for {c['id']} on the settings page"
            if row["default_text"] != f"Default: {c['default']}":
                return "fail", f"{c['id']}: default shows {row['default_text']!r}, table says {c['default']!r}"
            if row["cost_text"] != f"Cost: {c['cost']}":
                return "fail", f"{c['id']}: cost text differs from the table"
            if c["ui"]["kind"] == "readonly" and row["state_text"] != c["ui"]["state"]:
                return "fail", f"{c['id']}: read-only state shows {row['state_text']!r}, expected {c['ui']['state']!r}"
            if c["ui"]["kind"] == "link" and row.get("route") != c["ui"]["route"]:
                return "fail", f"{c['id']}: link row goes to {row.get('route')!r}, table says {c['ui']['route']!r}"
            want = default_label(c)
            if want is not None and row["live"] != want:
                return "fail", f"{c['id']}: the control shows {row['live']!r} on a fresh profile, default is {want!r}"
        return "pass", f"all {len(obs['rows'])} controls are listed with their default and cost; live controls show the defaults"


ATTACK_JS = """(() => {
  const out = {};
  for (const [k, u] of [['chrome_settings', 'chrome://settings/privacy/hardening'], ['chrome_flags', 'chrome://flags']]) {
    try { const w = window.open(u); out[k] = !!w; } catch (e) { out[k] = 'threw'; }
  }
  try { const w = window.open(location.protocol + '//' + location.host + '/hardening/idle_frame.html'); out.control = !!w; }
  catch (e) { out.control = 'threw'; }
  return out;
})()"""


@register
class Tamper(HardeningCheck):
    name = "hardening_tamper"
    control = "all"
    negative_control = ("In the same run a page opens an ordinary popup and the popup appears as a target, so "
                        "the absence of chrome:// windows is not because popups are blocked.")

    def observe(self, session: Session) -> dict:
        tbl = table()
        session.navigate_url(f"{site(session, 'a')}/index.html")
        # A user gesture makes this the strongest attack a page can mount.
        attacks = session.evaluate(ATTACK_JS, user_gesture=True)
        time.sleep(1.5)
        page_urls = [t["url"] for t in session.targets() if t.get("type") == "page"]
        session.navigate_url(f"{site(session, 'a')}/hardening/embed.html?site=b.localhost"
                             "&frame=/hardening/storage_access_frame.html")
        access = (page_value(session, "window.__result") or {}).get("result", "no answer")
        cookies = cookie_verdict(probe_cookies(session))
        probe = read_prefs(session, [c["pref"]["name"] for c in pref_controls(tbl)])
        return {"attacks": attacks, "page_urls": page_urls, "storage_access": access,
                "third_party_cookie": cookies, "prefs_mismatch": pref_mismatches(tbl, probe)}

    @staticmethod
    def judge(obs: dict) -> Verdict:
        urls = obs["page_urls"]
        if not any("idle_frame.html" in u for u in urls):
            return "fail", "negative control failed: an ordinary popup did not open, so blocked windows prove nothing"
        chrome_pages = [u for u in urls if u.startswith(("chrome://settings", "chrome://flags"))]
        if chrome_pages:
            return "fail", f"a page opened a privileged window: {chrome_pages}"
        if not str(obs["storage_access"]).startswith("rejected"):
            return "fail", f"requestStorageAccess without a gesture answered {obs['storage_access']!r}"
        if obs["third_party_cookie"] != "blocked":
            return "fail", f"after the attacks third-party cookies were {obs['third_party_cookie']}"
        if obs["prefs_mismatch"]:
            return "fail", "a hardened default changed: " + "; ".join(obs["prefs_mismatch"])
        return "pass", "window.open to chrome:// pages, requestStorageAccess and a cookie probe changed nothing"
