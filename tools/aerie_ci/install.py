"""Write the systemd user units to ~/.config/systemd/user. No root needed."""

from __future__ import annotations

import os
from pathlib import Path
from typing import Mapping

from . import steps as st

UNIT_DIR = Path(__file__).resolve().parent / "systemd"
UNITS = ("aerie-ci-poll.service", "aerie-ci-poll.timer", "aerie-ci-nightly.service", "aerie-ci-nightly.timer")
# --only name -> its timer unit. The PR gate runs from the poll timer.
TIMERS = {"poll": "aerie-ci-poll.timer", "nightly": "aerie-ci-nightly.timer"}


def default_dest(environ: Mapping[str, str] = os.environ) -> Path:
    config = environ.get("XDG_CONFIG_HOME") or str(Path(environ.get("HOME", str(Path.home()))) / ".config")
    return Path(config) / "systemd" / "user"


def install(dest: Path, repo: Path, *, shell: st.Shell | None = None, enable: bool = False,
            only: tuple[str, ...] | None = None) -> list[Path]:
    """Write every unit file. With enable, start the timers: all of them, or just those named in only."""
    timers = [unit for name, unit in TIMERS.items() if only is None or name in only]  # the CLI validates names
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
