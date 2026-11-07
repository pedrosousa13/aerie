"""Resident memory of a process tree, from /proc (Linux).

The Bench records it, with no threshold. The browser is many processes (browser,
GPU, renderers, utility), so the number that means something is the sum over
the tree under the browser's pid.
"""

from __future__ import annotations

from pathlib import Path


def _stat(proc: Path, pid: int) -> tuple[int, str] | None:
    """(ppid, comm) of a process, or None if it is gone or unreadable."""
    try:
        text = (proc / str(pid) / "stat").read_text()
    except (OSError, ValueError):
        return None
    # "pid (comm) state ppid ...": comm can hold spaces and parentheses.
    head, _, tail = text.rpartition(")")
    fields = tail.split()
    if len(fields) < 2:
        return None
    return int(fields[1]), head.partition("(")[2]


def _rss_kb(proc: Path, pid: int) -> int:
    try:
        for line in (proc / str(pid) / "status").read_text().splitlines():
            if line.startswith("VmRSS:"):
                return int(line.split()[1])
    except (OSError, ValueError, IndexError):
        pass
    return 0


def tree_rss_kb(root_pid: int, proc: Path = Path("/proc")) -> dict | None:
    """Total VmRSS in KiB of root_pid and all its descendants, with a per-process breakdown.

    Returns None where /proc is not available.
    """
    if not proc.is_dir():
        return None
    children: dict[int, list[int]] = {}
    for entry in proc.iterdir():
        if not entry.name.isdigit():
            continue
        info = _stat(proc, int(entry.name))
        if info:
            children.setdefault(info[0], []).append(int(entry.name))
    pids, queue = [], [root_pid]
    while queue:
        pid = queue.pop()
        if pid in pids:
            continue
        pids.append(pid)
        queue.extend(children.get(pid, []))
    parts = []
    for pid in sorted(pids):
        info = _stat(proc, pid)
        if info:
            parts.append({"pid": pid, "comm": info[1], "rss_kb": _rss_kb(proc, pid)})
    if not parts:
        return None
    return {"total_kb": sum(p["rss_kb"] for p in parts), "processes": parts}
