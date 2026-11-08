"""Keep tokens and secrets out of logs, statuses and issue text."""

from __future__ import annotations

import re
from typing import Mapping

_TOKEN_SHAPES = re.compile(
    r"(?:gh[pousr]_[A-Za-z0-9]{20,}|github_pat_[A-Za-z0-9_]{20,}|(?i:bearer)\s+[A-Za-z0-9._~+/=-]{8,})"
)
_SECRET_NAME = re.compile(r"TOKEN|SECRET|PASSWORD|PASSWD|CREDENTIAL|API_?KEY|PRIVATE_?KEY|^GH_|^GITHUB_|^SSH_", re.I)
MIN_SECRET_LENGTH = 8


def is_secret_name(name: str) -> bool:
    return bool(_SECRET_NAME.search(name))


class Redactor:
    def __init__(self, environ: Mapping[str, str]):
        values = {v for k, v in environ.items() if is_secret_name(k) and len(v) >= MIN_SECRET_LENGTH}
        self._values = sorted(values, key=len, reverse=True)

    def __call__(self, text: str) -> str:
        for value in self._values:
            text = text.replace(value, "[redacted]")
        return _TOKEN_SHAPES.sub("[redacted]", text)
