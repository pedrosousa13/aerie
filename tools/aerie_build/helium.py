"""Small pure helpers that mirror Helium's own build logic."""

from __future__ import annotations

from pathlib import Path

# Same constant as helium-chromium/utils/helium_version.py.
CHROME_VERSION_BASE = 136


def _first_number(path: Path) -> int:
    return int(path.read_text(encoding="utf-8").splitlines()[0].split(".")[0].strip())


def helium_version(core: Path, platform: Path) -> str:
    """Helium's MAJOR.MINOR.PATCH.PLATFORM version for a core + platform checkout."""
    return ".".join(
        str(n)
        for n in (
            _first_number(core / "version.txt"),
            _first_number(core / "chromium_version.txt") - CHROME_VERSION_BASE,
            _first_number(core / "revision.txt"),
            _first_number(platform / "revision.txt"),
        )
    )


class SafeBrowsingEnabled(ValueError):
    """The effective GN args would build Google Safe Browsing in."""


def safe_browsing_problems(args: str) -> list[str]:
    """Why these GN args would enable Safe Browsing, or an empty list.

    Aerie's phishing and malware protection is local only (ADR 0004). Helium
    compiles Safe Browsing out with `safe_browsing_mode=0`; Chromium's own
    default is on, so a missing assignment fails too. The last assignment wins,
    as in GN.
    """
    value = None
    for line in args.splitlines():
        name, sep, rest = line.partition("=")
        if sep and name.strip() == "safe_browsing_mode":
            value = rest.split("#")[0].strip()
    if value is None:
        return ["safe_browsing_mode is not set (Chromium's default builds Safe Browsing in); it must be 0"]
    if value != "0":
        return [f"safe_browsing_mode={value} builds Google Safe Browsing in; it must be 0"]
    return []


def gn_args(core_flags: str, platform_flags: str, arch: str, cc_wrapper: str | None) -> str:
    """args.gn contents, built the way helium-linux/scripts/shared.sh write_gn_args does."""
    parts = [core_flags.rstrip("\n"), platform_flags.rstrip("\n")]
    problems = safe_browsing_problems("\n".join(parts))
    if problems:
        raise SafeBrowsingEnabled("; ".join(problems))
    parts.append(f'target_cpu = "{arch}"')
    parts.append(f'v8_target_cpu = "{arch}"')
    if cc_wrapper:
        parts.append(f'cc_wrapper = "{cc_wrapper}"')
    return "\n".join(parts) + "\n"
