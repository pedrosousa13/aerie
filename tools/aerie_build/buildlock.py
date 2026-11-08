"""The build-tree lock: one exclusive flock on a file in the build root.

Everything that builds or rewrites the shared tree under the build root takes
this lock: ``./aerie build``, ``./aerie prune`` and the local CI. So a CI run
and a build train never touch the tree at the same time. (lock.py is the
source pin lock, a different thing.)

A CI run holds the lock around several steps, and one of them starts
``./aerie build`` in a child process. The holder exports ``AERIE_BUILD_LOCK_HELD``
(lock path and its own pid). A child of a live holder, for the same lock file,
does not take the lock again. The holder itself never skips it, and a marker
left behind by a dead process counts for nothing.
"""

from __future__ import annotations

import contextlib
import fcntl
import os
import time
from pathlib import Path
from typing import Callable, Iterator

LOCK_NAME = "build.lock"
ENV_HELD = "AERIE_BUILD_LOCK_HELD"
POLL_SECONDS = 5.0


class BuildTreeBusy(RuntimeError):
    """Another build holds the build-tree lock."""


def lock_path(root: Path) -> Path:
    return Path(root) / LOCK_NAME


def _held_by_live_parent(path: Path, environ: dict) -> bool:
    marker = environ.get(ENV_HELD, "")
    held_path, _, pid_text = marker.rpartition(":")
    if held_path != str(path) or not pid_text.isdigit():
        return False
    pid = int(pid_text)
    if pid == os.getpid():
        return False
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    return True


@contextlib.contextmanager
def build_tree_lock(
    root: Path,
    timeout: float | None = None,
    *,
    sleep: Callable[[float], None] = time.sleep,
    monotonic: Callable[[], float] = time.monotonic,
    poll: float = POLL_SECONDS,
    log: Callable[[str], None] | None = None,
) -> Iterator[None]:
    """Hold the exclusive build-tree lock. timeout: None waits forever, 0 does not wait."""
    path = lock_path(root)
    if _held_by_live_parent(path, os.environ):
        yield
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    fd = os.open(path, os.O_RDWR | os.O_CREAT, 0o600)
    try:
        deadline = None if timeout is None else monotonic() + timeout
        announced = False
        while True:
            try:
                fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
                break
            except BlockingIOError:
                if deadline is not None and monotonic() >= deadline:
                    raise BuildTreeBusy(
                        f"another build holds the build-tree lock ({path}); try again when it finishes"
                    ) from None
                if log and not announced:
                    log(f"waiting for the build-tree lock {path}")
                    announced = True
                sleep(poll)
        previous = os.environ.get(ENV_HELD)
        os.environ[ENV_HELD] = f"{path}:{os.getpid()}"
        try:
            yield
        finally:
            if previous is None:
                os.environ.pop(ENV_HELD, None)
            else:
                os.environ[ENV_HELD] = previous
    finally:
        os.close(fd)  # closing the descriptor drops the flock
