"""Fakes for the CI tests: no real gh, git, build or subprocess."""

from __future__ import annotations

from pathlib import Path

from aerie_ci.gh import PullRequest

SHA_A = "a" * 40
SHA_B = "b" * 40
ME = "pedrosousa13"


def pr(number=1, sha=SHA_A, author=ME, title="Some change"):
    return PullRequest(number=number, head_sha=sha, author=author, title=title)


class FakeGh:
    def __init__(self, prs=()):
        self.prs = list(prs)
        self.statuses: list[tuple[str, str, str]] = []  # sha, state, description
        self.issues: list[tuple] = []

    def open_prs(self):
        return list(self.prs)

    def post_status(self, sha, state, description):
        self.statuses.append((sha, state, description))

    def states(self, sha=None):
        return [s for (h, s, _) in self.statuses if sha in (None, h)]

    def create_issue(self, title, body):
        self.issues.append(("create", title, body))

    def comment_issue(self, number, body):
        self.issues.append(("comment", number, body))


class FakeShell:
    """Records every command. fail: substring of the joined argv -> exit code."""

    def __init__(self, fail=None, fetched_sha=SHA_A, checks="smoke ad_blocking branding zero_traffic scenario"):
        self.calls: list[list[str]] = []
        self.fail = fail or {}
        self.fetched_sha = fetched_sha
        self.checks = checks
        self.envs: list[dict] = []

    def run(self, argv, *, cwd=None, env=None, log=None):
        argv = [str(a) for a in argv]
        self.calls.append(argv)
        self.envs.append(dict(env or {}))
        if log is not None:
            log.write(f"$ {' '.join(argv)}\n")
        joined = " ".join(argv)
        for needle, code in self.fail.items():
            if needle in joined:
                return code
        return 0

    def capture(self, argv, *, cwd=None, env=None):
        argv = [str(a) for a in argv]
        self.calls.append(argv)
        if "rev-parse" in argv:
            return self.fetched_sha + "\n"
        if "-c" in argv:
            return self.checks + "\n"
        return ""

    def joined(self):
        return [" ".join(c) for c in self.calls]

    def ran(self, needle):
        return any(needle in c for c in self.joined())
