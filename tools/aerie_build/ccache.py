"""Compiler cache (ccache) configuration.

ccache reads $CCACHE_DIR/ccache.conf; max_size is the hard cap and ccache
evicts old entries once the cache grows past it.
"""

from __future__ import annotations

from pathlib import Path


def conf_text(max_size: str, cache_dir: Path) -> str:
    return (
        "# Written by ./aerie build. Edits are overwritten on the next build.\n"
        f"max_size = {max_size}\n"
        # Default is $XDG_RUNTIME_DIR, which is outside the build root.
        f"temporary_dir = {cache_dir / 'tmp'}\n"
        # Chromium embeds __DATE__/__TIME__ in a few files; see
        # https://chromium.googlesource.com/chromium/src/+/main/docs/linux/build_instructions.md#use-ccache
        "sloppiness = time_macros\n"
        "compression = true\n"
    )


def write_conf(cache_dir: Path, max_size: str) -> Path:
    cache_dir.mkdir(parents=True, exist_ok=True)
    conf = cache_dir / "ccache.conf"
    conf.write_text(conf_text(max_size, cache_dir))
    return conf
