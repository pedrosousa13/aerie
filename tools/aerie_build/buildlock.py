"""The build-tree lock: one exclusive flock on a file in the build root.

Everything that builds or rewrites the shared tree under the build root takes
this lock: ``./aerie build``, ``./aerie prune`` and the local CI. So a CI run
and a build train never touch the tree at the same time. (lock.py is the
source pin lock, a different thing.)

A CI run holds the lock around several steps, and one of them starts
``./aerie build`` in a child process. The holder exports ``AERIE_BUILD_LOCK_HELD``
(lock path and its own pid). A child of the holder, for the same lock file,
does not take the lock again. The marker counts only when its pid is an ancestor of the
process (read from /proc), so the holder itself never skips the lock, and a marker left in
a shell or naming a reused pid counts for nothing. A caller that copies os.environ before
taking the lock must add the marker the lock yields.
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


def try_flock(path: Path) -> int | None:
    """Open path (creating it and its folder) and take an exclusive flock without waiting.

    Returns the open descriptor, which holds the lock until it is closed, or None when another
    process holds it.
    """
    path.parent.mkdir(parents=True, exist_ok=True)
    fd = os.open(path, os.O_RDWR | os.O_CREAT, 0o600)
    try:
        fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except BlockingIOError:
        os.close(fd)
        return None
    except BaseException:
        os.close(fd)
        raise
    return fd


def _parent_of(pid: int) -> int | None:
    """The parent pid from /proc/<pid>/stat, or None when it cannot be read."""
    try:
        with open(f"/proc/{pid}/stat", encoding="utf-8", errors="replace") as fh:
            stat = fh.read()
        return int(stat.rpartition(")")[2].split()[1])  # after the command name, which may hold spaces
    except (OSError, ValueError, IndexError):
        return None  # includes PermissionError: not held


def _is_ancestor(pid: int) -> bool:
    current = os.getppid()
    seen = set()
    while current > 1 and current not in seen:
        if current == pid:
            return True
        seen.add(current)
        parent = _parent_of(current)
        if parent is None:
            return False
        current = parent
    return False


def held_by_ancestor(path: Path, environ: dict) -> bool:
    """True when environ carries the marker for this lock file and its pid is one of our ancestors."""
    marker = environ.get(ENV_HELD, "")
    held_path, _, pid_text = marker.rpartition(":")
    if held_path != str(path) or not pid_text.isdigit():
        return False
    return _is_ancestor(int(pid_text))


@contextlib.contextmanager
def build_tree_lock(
    root: Path,
    timeout: float | None = None,
    *,
    sleep: Callable[[float], None] = time.sleep,
    monotonic: Callable[[], float] = time.monotonic,
    poll: float = POLL_SECONDS,
    log: Callable[[str], None] | None = None,
) -> Iterator[str]:
    """Hold the exclusive build-tree lock. timeout: None waits forever, 0 does not wait.

    Yields the marker string that is also exported as AERIE_BUILD_LOCK_HELD. A caller that
    builds a child environment from a copy of os.environ made before the lock must add it.
    """
    path = lock_path(root)
    if held_by_ancestor(path, os.environ):
        yield os.environ[ENV_HELD]
        return
    deadline = None if timeout is None else monotonic() + timeout
    announced = False
    while (fd := try_flock(path)) is None:
        if deadline is not None and monotonic() >= deadline:
            raise BuildTreeBusy(
                f"another build holds the build-tree lock ({path}); try again when it finishes"
            ) from None
        if log and not announced:
            log(f"waiting for the build-tree lock {path}")
            announced = True
        sleep(poll)
    try:
        previous = os.environ.get(ENV_HELD)
        marker = f"{path}:{os.getpid()}"
        os.environ[ENV_HELD] = marker
        try:
            yield marker
        finally:
            if previous is None:
                os.environ.pop(ENV_HELD, None)
            else:
                os.environ[ENV_HELD] = previous
    finally:
        os.close(fd)  # closing the descriptor drops the flock
