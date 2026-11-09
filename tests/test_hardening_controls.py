import copy
import json
import unittest
from pathlib import Path

from aerie_hardening import model

REPO = Path(__file__).resolve().parent.parent


def good_control(**over):
    c = {
        "id": "demo_control",
        "title": "Demo control",
        "summary": "What it does.",
        "default": "On.",
        "provided_by": "chromium",
        "default_set_in": ["Chromium: chrome/browser/foo.cc"],
        "upstream": ["chrome/browser/foo.cc"],
        "aerie_patches": [],
        "pref": {"name": "demo.pref", "default": True},
        "cost": "Some sites break.",
        "ui": {"kind": "toggle", "pref": "demo.pref"},
        "bench_checks": ["hardening_demo"],
    }
    c.update(over)
    return c


def table(*controls):
    return {"schema": 1, "controls": list(controls), "deferred": [{"title": "T", "reason": "R"}],
            "locked_from_extensions": []}


class ValidatorTest(unittest.TestCase):
    def problems(self, tbl, checks=("hardening_demo",)):
        return model.validate(tbl, set(checks))

    def test_good_table_has_no_problems(self):
        self.assertEqual(self.problems(table(good_control())), [])

    def test_manual_test_is_optional_but_checked_when_present(self):
        tbl = table(good_control())
        good = {"intro": "I", "procedure": ["step"],
                "baseline": {"label": "L", "total_bits": "1.00",
                             "rows": [{"surface": "S", "value": "V", "bits": "1.00"}]}}
        tbl["manual_test"] = good
        self.assertEqual(self.problems(tbl), [])
        for broken in ({**good, "procedure": []},
                       {**good, "baseline": {**good["baseline"], "rows": []}},
                       {**good, "baseline": {**good["baseline"], "rows": [{"surface": "S|x", "value": "V", "bits": "1"}]}},
                       {k: v for k, v in good.items() if k != "intro"}):
            tbl["manual_test"] = broken
            self.assertTrue(any("manual_test" in p for p in self.problems(tbl)), broken)

    def test_missing_required_key_is_reported(self):
        c = good_control()
        del c["cost"]
        self.assertTrue(any("cost" in p for p in self.problems(table(c))))

    def test_provided_by_must_be_in_enum(self):
        self.assertTrue(any("provided_by" in p for p in self.problems(table(good_control(provided_by="brave")))))

    def test_duplicate_and_malformed_ids(self):
        dup = self.problems(table(good_control(), good_control()))
        self.assertTrue(any("duplicate" in p for p in dup))
        self.assertTrue(any("id" in p for p in self.problems(table(good_control(id="Bad Id")))))

    def test_bench_check_must_exist(self):
        self.assertTrue(any("hardening_nope" in p for p in
                            self.problems(table(good_control(bench_checks=["hardening_nope"])))))

    def test_every_control_needs_a_bench_check(self):
        self.assertTrue(any("bench_checks" in p for p in self.problems(table(good_control(bench_checks=[])))))

    def test_aerie_control_names_its_patches_and_they_must_exist(self):
        c = good_control(provided_by="aerie", aerie_patches=[])
        self.assertTrue(any("aerie_patches" in p for p in self.problems(table(c))))
        c = good_control(provided_by="aerie", aerie_patches=["hardening/0099-missing.patch"])
        probs = model.validate(table(c), {"hardening_demo"}, series=["hardening/0001-a.patch"])
        self.assertTrue(any("0099-missing" in p for p in probs))
        ok = model.validate(table(good_control(provided_by="aerie", aerie_patches=["hardening/0001-a.patch"])),
                            {"hardening_demo"}, series=["hardening/0001-a.patch"])
        self.assertEqual(ok, [])

    def test_ui_kinds(self):
        dd = {"kind": "dropdown", "pref": "demo.pref", "default_value": 1,
              "options": [{"value": 1, "label": "One"}, {"value": 2, "label": "Two"}]}
        self.assertEqual(self.problems(table(good_control(ui=dd, pref={"name": "demo.pref", "default": 1}))), [])
        bad = dict(dd, default_value=9)
        self.assertTrue(any("default_value" in p for p in self.problems(table(good_control(ui=bad)))))
        self.assertTrue(any("kind" in p for p in self.problems(table(good_control(ui={"kind": "slider"})))))
        ro = {"kind": "readonly", "state": "Always on"}
        self.assertEqual(self.problems(table(good_control(ui=ro, pref=None))), [])
        self.assertTrue(any("state" in p for p in self.problems(table(good_control(ui={"kind": "readonly"})))))

    def test_link_kind_needs_a_route_name(self):
        link = {"kind": "link", "route": "SITE_SETTINGS_JAVASCRIPT_OPTIMIZER"}
        self.assertEqual(self.problems(table(good_control(ui=link, pref=None))), [])
        self.assertTrue(any("route" in p for p in self.problems(table(good_control(ui={"kind": "link"}, pref=None)))))
        for bad in ("site_settings", "A B", "A;B", ""):
            self.assertTrue(any("route" in p for p in
                                self.problems(table(good_control(ui={"kind": "link", "route": bad}, pref=None)))), bad)

    def test_dropdown_default_must_match_pref_default(self):
        dd = {"kind": "dropdown", "pref": "demo.pref", "default_value": 2,
              "options": [{"value": 1, "label": "One"}, {"value": 2, "label": "Two"}]}
        self.assertTrue(any("does not match" in p for p in
                            self.problems(table(good_control(ui=dd, pref={"name": "demo.pref", "default": 1})))))

    def test_text_cannot_carry_template_syntax(self):
        for bad in ("[[x]]", "{{x}}", "$i18n{x}", "<b>", "${x}"):
            self.assertTrue(any("template" in p or "markup" in p for p in
                                self.problems(table(good_control(cost=f"x {bad} y")))), bad)

    def test_deferred_entries_need_title_and_reason(self):
        t = table(good_control())
        t["deferred"] = [{"title": "only"}]
        self.assertTrue(any("deferred" in p for p in self.problems(t)))

    def test_locked_from_extensions_entries(self):
        t = table(good_control())
        t["locked_from_extensions"] = [{"extension_pref": "webRTCIPHandlingPolicy", "browser_pref": "webrtc.ip_handling_policy"}]
        self.assertEqual(self.problems(t), [])
        t["locked_from_extensions"] = [{"extension_pref": "x"}]
        self.assertTrue(any("locked_from_extensions" in p for p in self.problems(t)))


class RealTableTest(unittest.TestCase):
    def setUp(self):
        self.table = model.load()

    def test_ids_cover_the_issue_controls(self):
        ids = {c["id"] for c in self.table["controls"]}
        self.assertTrue({"https_only", "third_party_cookies", "storage_partitioning", "webrtc_ip",
                         "prefetch", "site_isolation", "fingerprinting", "extension_lock"} <= ids)

    def test_decisions_from_the_issue_are_in_the_table(self):
        by_id = {c["id"]: c for c in self.table["controls"]}
        https = by_id["https_only"]
        self.assertEqual(https["pref"], {"name": "https_only_mode_enabled", "default": True})
        self.assertEqual(https["provided_by"], "aerie")
        self.assertEqual(by_id["site_isolation"]["ui"], {"kind": "readonly", "state": "Always on"})
        jit = by_id["js_optimizer"]
        self.assertEqual(jit["ui"], {"kind": "link", "route": "SITE_SETTINGS_JAVASCRIPT_OPTIMIZER"})
        self.assertEqual(jit["provided_by"], "chromium")
        self.assertEqual(jit["bench_checks"], ["hardening_js_optimizer"])
        deferred = " ".join(d["title"] for d in self.table["deferred"]).lower()
        for word in ("client rects", "measuretext", "webgl"):
            self.assertIn(word, deferred)
        locked = {e["browser_pref"] for e in self.table["locked_from_extensions"]}
        self.assertTrue({"webrtc.ip_handling_policy", "net.network_prediction_options",
                         "profile.cookie_controls_mode"} <= locked)

    def test_patch_names_are_in_the_series_and_checks_exist(self):
        from aerie_build import patches
        from aerie_bench import checks
        series = patches.parse_series(REPO / "patches" / "series")
        self.assertEqual(model.validate(self.table, set(checks.REGISTRY), series=series), [])

    def test_every_hardening_check_belongs_to_a_control_and_has_a_negative_control(self):
        from aerie_bench import checks
        ids = {c["id"] for c in self.table["controls"]} | {"all"}
        named = {n for c in self.table["controls"] for n in c["bench_checks"]}
        named |= set(self.table.get("cross_cutting_checks", []))
        hardening = {n: cls for n, cls in checks.REGISTRY.items() if n.startswith("hardening_")}
        # Every "hardening_" check belongs to a control, and every check a control names is either one
        # of those or a feature check listed here on purpose (it has no control id of its own).
        feature_checks = {"widevine_opt_in"}
        self.assertEqual(set(hardening), named - feature_checks)
        self.assertLessEqual(feature_checks, named)
        self.assertLessEqual(named, set(checks.REGISTRY))
        for name, cls in hardening.items():
            self.assertIn(cls.control, ids, name)
            self.assertTrue(cls.negative_control.strip(), f"{name} has no negative control")


if __name__ == "__main__":
    unittest.main()
