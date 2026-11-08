# Local CI

CI runs on the Linux build machine, as the maintainer's user, from systemd
user timers. It uses no GitHub Actions runner and no sudo. The commands are
plain CLI steps, so a later Actions workflow on a self-hosted runner (#14) can
call the same ones.

The recommended setup enables only the nightly (see "Install"). `install` with no
`--only` writes all four unit files and, with `--enable`, enables both timers. The
per-PR gate is off in the recommended setup because the build train is the merge
gate. You can still run it by hand or enable the poll timer later.

```sh
python3 -m tools.aerie_ci poll        # gate the maintainer's PRs that have a new head SHA
python3 -m tools.aerie_ci gate 42     # gate one PR now
python3 -m tools.aerie_ci nightly     # clean build and full Bench on main
python3 -m tools.aerie_ci install     # write the systemd user units
```

Run them from the repo root. Python 3.11+, standard library only, plus `gh` and `git`.
Options for `poll` and `gate`: `--build-root DIR` (env `AERIE_BUILD_ROOT`) and
`--lock-timeout SEC`. `nightly` takes its own root and cache options (see "Nightly").

## PR gate

`poll` lists open PRs with `gh pr list`. For each PR it does these things, in this order:

1. **Author check.** Only PRs authored by `pedrosousa13` go on. For every other
   author, nothing runs: no fetch, no checkout, no build, no status.
2. **Dedupe.** A head SHA that is in `<CI folder>/gated.json` is not gated again. A new push gives a new SHA, so it is gated.
3. Posts the commit status `aerie/gate` as `pending`.
4. Takes the build-tree lock, fetches `refs/pull/<n>/head`, checks that it still
   is the SHA that was listed, and makes a detached worktree of it under
   `<CI folder>/worktrees/<sha12>`.
5. Runs, in order, and stops at the first failure: patch check
   (`./aerie patches check`), incremental build (`./aerie build`), the Python
   unit tests, a smoke launch (`./aerie bench --check smoke`), and the Bench
   with every other check except `zero_traffic` (that one idles for minutes; the nightly runs it).
   Each step shows in the pending status description.
6. Removes the worktree, posts `success` or `failure`, and records the SHA.

The status description holds a one-line summary and the log path
(`<CI folder>/logs/<sha12>/gate.log`; `summary.json` is next to it).
If the build tree is busy longer than the lock timeout (default 6 hours), the
status is `error` ("Build tree busy") and the SHA is not recorded, so the next poll tries again.
A harness error (git or gh broke) also posts `error`, and that SHA is recorded.

C++ unit tests join the gate when any can run (#54).

## Nightly

`nightly` fetches `main`, builds it with `./aerie build --clean`, and runs the
full Bench (every registered check, `zero_traffic` included). It does this in its
own build root, so it never replaces the build the maintainer tests from.

| Setting | Flag | Environment | Default |
| --- | --- | --- | --- |
| Nightly build root | `--build-root DIR` | `AERIE_NIGHTLY_BUILD_ROOT` | `/mnt/linux2/aerie-build-nightly` |
| Shared compiler cache | `--ccache-dir DIR` | `AERIE_CCACHE_DIR` | `<main build root>/ccache` |

`AERIE_BUILD_ROOT` names the main root (the build train's). The nightly never uses it as
its own root, and refuses a root that is the main root, inside it, or around it (symlinks
are resolved first).

**If the train uses a build root other than the default** (`/mnt/linux2/aerie-build`), the
nightly must be told. It reads the main root only from `AERIE_BUILD_ROOT`, and the
`<main build root>/ccache` default follows that value. When the train was started with
`--build-root` and `AERIE_BUILD_ROOT` is not set for the nightly's unit, the nightly would
use the default root's `ccache` folder, which is not the train's cache. In that case set
`AERIE_CCACHE_DIR` to the train's cache folder (with `systemctl --user edit
aerie-ci-nightly.service`, add `Environment=AERIE_CCACHE_DIR=...`), or set `AERIE_BUILD_ROOT`.
A cache folder that does not match costs hits, not correctness.

The nightly root holds the source tree, `out/`, downloads, the toolchain and the Bench
output (`<nightly root>/bench/<date>/`). Only the compiler cache is outside it: the nightly
points `./aerie build` at the main root's cache with `--ccache-dir`. The log and summary
are in `<CI folder>/nightly/<date>/` (`nightly.log`, `summary.json`); the CI folder is
`<nightly root>-ci`, so `/mnt/linux2/aerie-build-nightly-ci` by default.

`--clean` removes the nightly's source tree and pristine snapshot, but the compiler
cache stays, so it is a clean tree and not a cold cache.

For a hit to cross the two trees, ccache must not see their different paths. Aerie does two things:

- Every `./aerie` command sets `CCACHE_BASEDIR` to that root's `src` folder in the build
  environment. ccache then rewrites absolute paths under it to relative ones before it hashes.
  It is an environment variable and not a `ccache.conf` line because the conf file is shared
  by both roots, and each needs its own value.
- `ccache.conf` has `hash_dir = false`, so the build directory (`out/Default` in each tree)
  is not part of the hash.

Be careful with the claim. ccache helps little on this build (see the README timing section:
Chromium compiles most C++ with Clang modules, which ccache does not cache by default), and
`hash_dir = false` can make debug info point at the other tree's paths. These settings remove
the path difference. Nobody has measured a hit rate between the two trees yet. Check the
`ccache --show-stats` lines in the first nightly build log.

### The compiler cache and two trees at once

The nightly and a build train can compile at the same time, both with `CCACHE_DIR` set to
the main root's `ccache` folder. ccache is built for that: it writes each entry to a
temporary file and renames it into place, and it takes its own per-file locks. Aerie adds no
lock for it. Two things to know:

- Both builds write `ccache.conf` at the start (cap from `--ccache-size` or `AERIE_CCACHE_SIZE`,
  default `30G`). Give the nightly the same cap as the train, or the later writer wins.
- `./aerie prune` runs `ccache --cleanup` and trims the cache to its cap (ccache also evicts old entries
  as it writes). A trim while the other build runs can remove entries that build would have used. That costs a
  miss, not a wrong result.

The nightly takes `build.lock` in its own root only. It does not wait for the main root's
lock, and a build train does not wait for it, because they share no tree. If you point
`--build-root` at the main root (this is refused) they would share everything and need that lock.

Nothing else normally takes the nightly root's lock. Only a `./aerie build`, `test-cpp`, `prune`
or `bench` that you start by hand in that root does. So the nightly waits at most 1 hour for it
(`--lock-timeout`, default 3600 seconds) and then reports "busy". A longer wait would only hide a
stuck process. The systemd unit allows 6 hours in all: up to 1 hour for the lock, then the clean
build (about 2.5 hours on the reference machine, README timing section) and the full Bench, with
room to spare.

A failure, or a night when the nightly root stayed busy, opens an issue titled
"Nightly CI failed <date>". Set `AERIE_CI_NIGHTLY_ISSUE=<number>` to comment on
a standing issue instead. A success reports nothing.

### Disk space

A second Chromium tree is large: source, `out/Default` and downloads are tens of GB, on top of
the main root. Check free space on the volume that holds `/mnt/linux2/aerie-build-nightly`
before the first run (`df -h /mnt/linux2`). `./aerie prune --build-root /mnt/linux2/aerie-build-nightly`
removes stale downloads and old trees there.

## Where CI files live

The CI folder is `<build root>-ci`, a sibling of the build root (for example
`/mnt/linux2/aerie-build-ci` for the gate, `/mnt/linux2/aerie-build-nightly-ci` for the nightly). It holds `gated.json`, `poll.lock`, `logs/`, `nightly/`
and `worktrees/`. It is not inside the build root, because `./aerie` refuses a
checkout inside its build root and each gate worktree is a checkout. The tree,
downloads and `build.lock` stay in the build root.

## The build-tree lock

`<build root>/build.lock` is an exclusive `flock`. `./aerie build`, `./aerie test-cpp`,
`./aerie prune`, `./aerie bench` and every CI run take it, so a CI run and a build train
never use the shared tree together. `--lock-timeout SEC` (on all four commands) sets how
long to wait (default: forever; `0` fails at once).

A CI run holds the lock across all its steps. The holder exports
`AERIE_BUILD_LOCK_HELD` (lock path and its pid) and the CI passes it on in each
step's environment. The `./aerie build` or `./aerie bench` a step starts finds the
marker and does not wait for its parent. A marker counts only when its pid is an
ancestor of the process (read from `/proc`), so a marker left in a shell, or a
reused pid, does not skip the lock.

### Holding the lock across a train's build and Bench

`./aerie build` and `./aerie bench` each take the lock for their own run only. Between
them the lock is free, so the poller could rebuild `out/Default` and the Bench would
test the PR's binary. A build train that runs both must hold the lock across them:

```sh
./aerie locked -- sh -c './aerie build && ./aerie bench --check zero_traffic'
```

`./aerie locked [--lock-timeout SEC] -- CMD...` takes the lock, runs the command
with the marker set, and exits with the command's exit code. The `./aerie build`
and `./aerie bench` inside do not wait. (`tools/aerie_build/lock.py` is the source pin lock, a different thing.)

A gate or nightly that is killed mid-run can leave its worktree behind. The next run
removes and prunes it before it makes a new one. A harness error is not recorded as
gated, so the next poll gates that head again.

## Install

Recommended: the nightly only.

```sh
python3 -m tools.aerie_ci install --enable --only nightly
```

This writes all four unit files to `~/.config/systemd/user/`, then runs `daemon-reload` and
`enable --now aerie-ci-nightly.timer`. The poll units are written but not enabled, so the
gate stays available (`gate N` by hand, or `systemctl --user enable --now aerie-ci-poll.timer`
later) and off.

```sh
python3 -m tools.aerie_ci install            # writes four files to ~/.config/systemd/user/, starts nothing
python3 -m tools.aerie_ci install --enable   # also enable --now both timers
```

`--only` takes `poll` or `nightly`, can be repeated, and needs `--enable`.

| Unit | Runs |
| --- | --- |
| `aerie-ci-poll.timer` | `poll` 2 minutes after boot, then 10 minutes after each run ends |
| `aerie-ci-nightly.timer` | `nightly` at 02:30 (with a small random delay; runs at next start if missed) |

The unit templates are in `tools/aerie_ci/systemd/`; `install` fills in the repo path.
The units run from the repo checkout, so keep that checkout on `main`. To see runs: `journalctl --user -u aerie-ci-nightly`.

## Branch protection

Only if you run the gate. With the nightly-only setup, do not do this: the build train is the merge gate, and a required `aerie/gate` check would block every PR because nothing posts it.

One step, done once by the maintainer after the first `aerie/gate` status exists:

```sh
gh api -X PUT repos/pedrosousa13/aerie/branches/main/protection --input - <<'JSON'
{"required_status_checks": {"strict": false, "contexts": ["aerie/gate"]},
 "enforce_admins": false, "required_pull_request_reviews": null, "restrictions": null}
JSON
```

Check the existing protection first (`gh api repos/pedrosousa13/aerie/branches/main/protection`); this call replaces it.

## Security

The repo is public, so code from anyone else must never run here.

- The author check is the first thing `gate_pr` does. A test checks that no command runs for another author.
- Each gate run uses its own worktree. Nothing from the PR runs before the author check.
- Steps run with the caller's environment minus names that look like credentials (`*TOKEN*`, `*SECRET*`, `GH_*`, `GITHUB_*`, `SSH_*`, ...).
  This is not a sandbox. A PR runs as the maintainer's user, so the author check is the real boundary.
- Logs, statuses, summaries and issue text are redacted: token shapes (`ghp_...`, `github_pat_...`, `Bearer ...`)
  and the values of secret-looking environment variables. `gh` keeps its own login; CI never reads it.

## Tests

```sh
python3 -m unittest tests.test_build_lock tests.test_ci_gh tests.test_ci_gate tests.test_ci_nightly tests.test_ci_install_cli -v
```

The gate and nightly tests mock `gh`, `git` and the build commands. These use real files
and real processes instead:

- the lock tests (`tests.test_build_lock`) and the nightly's separate-root lock test, with a real `flock`;
- `tests.test_ci_real`: the nightly root and cache resolution against real temp dirs (symlinks
  included), and `./aerie prune --ccache-dir DIR` in a real child process, with a stand-in
  `ccache` that records the `CCACHE_DIR` and `CCACHE_BASEDIR` it receives.

Run `tests.test_ci_real` together with the others:

```sh
python3 -m unittest tests.test_ci_real tests.test_ccache -v
```
