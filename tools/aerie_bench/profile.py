"""Throwaway browser profile: created fresh, always deleted."""

from __future__ import annotations

import json
import os
import shutil
import tempfile
from contextlib import contextmanager
from pathlib import Path
from typing import Iterator


# A top-level key of the dict given to seed_prefs: its value is written to the profile's
# "Local State" file (browser-wide prefs) instead of Default/Preferences.
LOCAL_STATE_KEY = "__local_state__"


class Profile:
    def __init__(self, path: Path) -> None:
        self.path = path
        self.deleted = False

    def seed_prefs(self, prefs: dict) -> None:
        """Write Default/Preferences before the first launch.

        ``prefs[LOCAL_STATE_KEY]``, if present, goes to ``Local State`` instead.
        """
        prefs = dict(prefs)
        local_state = prefs.pop(LOCAL_STATE_KEY, None)
        default = self.path / "Default"
        default.mkdir(mode=0o700, exist_ok=True)
        (default / "Preferences").write_text(json.dumps(prefs))
        if local_state is not None:
            (self.path / "Local State").write_text(json.dumps(local_state))

    def delete(self) -> None:
        shutil.rmtree(self.path, ignore_errors=True)
        self.deleted = not self.path.exists()


@contextmanager
def fresh_profile(root: Path) -> Iterator[Profile]:
    """A new 0700 directory under ``root``, removed on success, error or interrupt."""
    root.mkdir(parents=True, exist_ok=True)
    profile = Profile(Path(tempfile.mkdtemp(prefix="profile-", dir=root)))
    os.chmod(profile.path, 0o700)
    try:
        yield profile
    finally:
        profile.delete()
