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


def _descendants(proc: Path, root_pid: int) -> list[int]:
    """root_pid and every process below it, found by walking parent pids under ``proc``."""
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
    return pids


def tree_rss_kb(root_pid: int, proc: Path = Path("/proc")) -> dict | None:
    """Total VmRSS in KiB of root_pid and all its descendants, with a per-process breakdown.

    Returns None where /proc is not available.
    """
    if not proc.is_dir():
        return None
    pids = _descendants(proc, root_pid)
    parts = []
    for pid in sorted(pids):
        info = _stat(proc, pid)
        if info:
            parts.append({"pid": pid, "comm": info[1], "rss_kb": _rss_kb(proc, pid)})
    if not parts:
        return None
    return {"total_kb": sum(p["rss_kb"] for p in parts), "processes": parts}


def renderer_modes(root_pid: int, proc: Path = Path("/proc")) -> dict | None:
    """Which renderers under ``root_pid`` run without V8's optimizing compilers.

    Reads each descendant's command line. A renderer started for a site whose JIT is off
    carries ``--disable-optimizing-compilers`` in its ``--js-flags`` (``--jitless`` when the
    whole JIT is off). Returns ``{"total", "noopt", "jitless", "pids"}`` counting renderers
    only, or None where /proc is not available.
    """
    if not proc.is_dir():
        return None
    seen = _descendants(proc, root_pid)
    out = {"total": 0, "noopt": 0, "jitless": 0, "pids": {"noopt": [], "default": []}}
    for pid in sorted(seen):
        try:
            args = (proc / str(pid) / "cmdline").read_bytes().split(b"\0")
        except OSError:
            continue
        # A renderer forked from the zygote rewrites its title, so /proc shows the whole command
        # line as ONE space-joined argument. Split on whitespace as well (switch values here have
        # no spaces: AppendToCommaSeparatedSwitch joins js-flags with commas).
        text = [t for a in args if a for t in a.decode("utf-8", "replace").split()]
        if "--type=renderer" not in text:
            continue
        out["total"] += 1
        flags = " ".join(a for a in text if a.startswith("--js-flags="))
        if "--jitless" in flags:
            out["jitless"] += 1
        if "--disable-optimizing-compilers" in flags:
            out["noopt"] += 1
            out["pids"]["noopt"].append(pid)
        else:
            out["pids"]["default"].append(pid)
    return out
