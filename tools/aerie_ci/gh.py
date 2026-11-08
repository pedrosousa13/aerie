"""The only code that talks to GitHub, through the gh CLI (which holds its own login).

This module never reads or passes a token. Everything it sends is redacted.
"""

from __future__ import annotations

import json
import os
import subprocess
from dataclasses import dataclass
from typing import Callable, Sequence

from .redact import Redactor

REPO = "pedrosousa13/aerie"
CONTEXT = "aerie/gate"
STATES = ("pending", "success", "failure", "error")
DESCRIPTION_LIMIT = 140  # GitHub's limit for a commit status description


class GhError(RuntimeError):
    pass


@dataclass(frozen=True)
class PullRequest:
    number: int
    head_sha: str
    author: str
    title: str


def _default_run(argv: Sequence[str]) -> subprocess.CompletedProcess:
    return subprocess.run(list(argv), capture_output=True, text=True, timeout=120)


class Gh:
    def __init__(self, repo: str = REPO, run: Callable[[Sequence[str]], subprocess.CompletedProcess] = _default_run,
                 redact: Redactor | None = None):
        self.repo = repo
        self._run = run
        self._redact = redact or Redactor(os.environ)

    def _gh(self, *args: str) -> str:
        proc = self._run(["gh", *args])
        if proc.returncode != 0:
            raise GhError(f"gh {' '.join(args[:2])} failed: {self._redact((proc.stderr or '').strip())}")
        return proc.stdout

    def open_prs(self) -> list[PullRequest]:
        out = self._gh("pr", "list", "-R", self.repo, "--state", "open", "--limit", "100",
                       "--json", "number,headRefOid,author,title")
        return [
            PullRequest(
                number=int(item["number"]),
                head_sha=item["headRefOid"],
                author=(item.get("author") or {}).get("login", ""),
                title=item.get("title", ""),
            )
            for item in json.loads(out)
        ]

    def post_status(self, sha: str, state: str, description: str) -> None:
        if state not in STATES:
            raise ValueError(f"bad commit status state {state!r}")
        description = self._redact(description)[:DESCRIPTION_LIMIT]
        self._gh("api", f"repos/{self.repo}/statuses/{sha}",
                 "-f", f"state={state}", "-f", f"context={CONTEXT}", "-f", f"description={description}")

    def create_issue(self, title: str, body: str) -> None:
        self._gh("issue", "create", "-R", self.repo, "--title", self._redact(title), "--body", self._redact(body))

    def comment_issue(self, number: int, body: str) -> None:
        self._gh("issue", "comment", str(number), "-R", self.repo, "--body", self._redact(body))
