"""Write the systemd user units to ~/.config/systemd/user. No root needed."""

from __future__ import annotations

import os
from pathlib import Path
from typing import Mapping

from . import steps as st

UNIT_DIR = Path(__file__).resolve().parent / "systemd"
UNITS = ("aerie-ci-poll.service", "aerie-ci-poll.timer", "aerie-ci-nightly.service", "aerie-ci-nightly.timer")
TIMERS = ("aerie-ci-poll.timer", "aerie-ci-nightly.timer")
TIMER_NAMES = ("poll", "nightly")  # for --only; the gate runs from the poll timer


def default_dest(environ: Mapping[str, str] = os.environ) -> Path:
    config = environ.get("XDG_CONFIG_HOME") or str(Path(environ.get("HOME", str(Path.home()))) / ".config")
    return Path(config) / "systemd" / "user"


def install(dest: Path, repo: Path, *, shell: st.Shell | None = None, enable: bool = False,
            only: tuple[str, ...] | None = None) -> list[Path]:
    """Write every unit file. With enable, start the timers: all of them, or just those named in only."""
    unknown = set(only or ()) - set(TIMER_NAMES)
    if unknown:
        raise ValueError(f"unknown timer {sorted(unknown)}; choose from {list(TIMER_NAMES)}")
    timers = [t for t in TIMERS if only is None or t.split("-")[2].split(".")[0] in only]
    dest.mkdir(parents=True, exist_ok=True)
    written = []
    for name in UNITS:
        text = (UNIT_DIR / name).read_text().replace("@REPO@", str(repo))
        target = dest / name
        target.write_text(text)
        written.append(target)
    if enable:
        shell = shell or st.Shell()
        shell.capture(["systemctl", "--user", "daemon-reload"])
        shell.capture(["systemctl", "--user", "enable", "--now", *timers])
    return written
