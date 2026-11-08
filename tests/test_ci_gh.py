import json
import subprocess
import unittest
from unittest import mock

from aerie_ci import gh as ghmod
from aerie_ci.redact import Redactor


def completed(stdout="", rc=0):
    return subprocess.CompletedProcess([], rc, stdout=stdout, stderr="")


class GhTest(unittest.TestCase):
    def test_open_prs_parses_gh_json(self):
        out = json.dumps([
            {"number": 7, "headRefOid": "c" * 40, "author": {"login": "someone"}, "title": "T"},
            {"number": 8, "headRefOid": "d" * 40, "author": {"login": "pedrosousa13"}, "title": "U"},
        ])
        run = mock.Mock(return_value=completed(out))
        prs = ghmod.Gh(run=run).open_prs()
        self.assertEqual([(p.number, p.head_sha, p.author) for p in prs],
                         [(7, "c" * 40, "someone"), (8, "d" * 40, "pedrosousa13")])
        argv = run.call_args.args[0]
        self.assertEqual(argv[:3], ["gh", "pr", "list"])
        self.assertIn("pedrosousa13/aerie", argv)

    def test_a_pr_without_an_author_is_kept_with_an_empty_login(self):
        out = json.dumps([{"number": 1, "headRefOid": "e" * 40, "author": None, "title": ""}])
        prs = ghmod.Gh(run=mock.Mock(return_value=completed(out))).open_prs()
        self.assertEqual(prs[0].author, "")

    def test_post_status_uses_the_statuses_api_with_the_gate_context(self):
        run = mock.Mock(return_value=completed("{}"))
        ghmod.Gh(run=run).post_status("f" * 40, "pending", "Running: build")
        argv = run.call_args.args[0]
        self.assertEqual(argv[:3], ["gh", "api", f"repos/pedrosousa13/aerie/statuses/{'f' * 40}"])
        self.assertIn("context=aerie/gate", argv)
        self.assertIn("state=pending", argv)
        self.assertIn("description=Running: build", argv)

    def test_status_description_is_redacted_and_cut_to_github_limit(self):
        run = mock.Mock(return_value=completed("{}"))
        token = "ghp_" + "A" * 36
        ghmod.Gh(run=run).post_status("f" * 40, "failure", f"x {token} " + "y" * 300)
        desc = next(a for a in run.call_args.args[0] if a.startswith("description="))
        self.assertNotIn(token, desc)
        self.assertLessEqual(len(desc) - len("description="), 140)

    def test_unknown_state_is_refused(self):
        with self.assertRaises(ValueError):
            ghmod.Gh(run=mock.Mock()).post_status("f" * 40, "passed", "x")

    def test_gh_failure_raises_without_echoing_the_environment(self):
        run = mock.Mock(return_value=subprocess.CompletedProcess([], 1, stdout="", stderr="HTTP 401"))
        with self.assertRaises(ghmod.GhError) as cm:
            ghmod.Gh(run=run).open_prs()
        self.assertIn("HTTP 401", str(cm.exception))

    def test_nightly_report_helpers(self):
        run = mock.Mock(return_value=completed())
        g = ghmod.Gh(run=run)
        g.create_issue("Nightly failed", "body")
        self.assertEqual(run.call_args.args[0][:3], ["gh", "issue", "create"])
        g.comment_issue(15, "body")
        self.assertEqual(run.call_args.args[0][:4], ["gh", "issue", "comment", "15"])


class RedactorTest(unittest.TestCase):
    def test_token_shapes_are_removed(self):
        r = Redactor({})
        for secret in ("ghp_" + "a" * 36, "github_pat_" + "B" * 40, "gho_" + "c" * 30):
            self.assertNotIn(secret, r(f"before {secret} after"))
        self.assertNotIn("abcdef123456", r("Authorization: Bearer abcdef123456"))

    def test_values_of_secret_looking_env_vars_are_removed(self):
        r = Redactor({"MY_API_TOKEN": "hunter2hunter2", "PATH": "/usr/bin", "GH_PASSWORD": "short"})
        self.assertEqual(r("x hunter2hunter2 y /usr/bin"), "x [redacted] y /usr/bin")

    def test_plain_text_is_unchanged(self):
        self.assertEqual(Redactor({})("build ok in 2h"), "build ok in 2h")
