# Local CI

CI runs on the Linux build machine, as the maintainer's user, from two systemd
user timers. It uses no GitHub Actions runner and no sudo. The commands are
plain CLI steps, so a later Actions workflow on a self-hosted runner (#14) can
call the same ones.

```sh
python3 -m tools.aerie_ci poll        # gate the maintainer's PRs that have a new head SHA
python3 -m tools.aerie_ci gate 42     # gate one PR now
python3 -m tools.aerie_ci nightly     # clean build and full Bench on main
python3 -m tools.aerie_ci install     # write the systemd user units
```

Run them from the repo root. Python 3.11+, standard library only, plus `gh` and `git`.
Options for `poll`, `gate` and `nightly`: `--build-root DIR` (env `AERIE_BUILD_ROOT`)
and `--lock-timeout SEC`.

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
full Bench (every registered check, `zero_traffic` included). Results are in
`<CI folder>/nightly/<date>/`: `nightly.log`, `summary.json` and `bench/`.
`--clean` removes the source tree and the pristine snapshot, but the compiler
cache stays, so it is a clean tree and not a cold cache.

A failure, or a night when the build tree stayed busy, opens an issue titled
"Nightly CI failed <date>". Set `AERIE_CI_NIGHTLY_ISSUE=<number>` to comment on
a standing issue instead. A success reports nothing.

## Where CI files live

The CI folder is `<build root>-ci`, a sibling of the build root (for example
`/mnt/linux2/aerie-build-ci`). It holds `gated.json`, `poll.lock`, `logs/`, `nightly/`
and `worktrees/`. It is not inside the build root, because `./aerie` refuses a
checkout inside its build root and each gate worktree is a checkout. The shared tree,
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

```sh
python3 -m tools.aerie_ci install            # writes four files to ~/.config/systemd/user/
python3 -m tools.aerie_ci install --enable   # also daemon-reload and enable --now both timers
```

| Unit | Runs |
| --- | --- |
| `aerie-ci-poll.timer` | `poll` 2 minutes after boot, then 10 minutes after each run ends |
| `aerie-ci-nightly.timer` | `nightly` at 02:30 (with a small random delay; runs at next start if missed) |

The unit templates are in `tools/aerie_ci/systemd/`; `install` fills in the repo path.
The units run from the repo checkout, so keep that checkout on `main`. To see runs: `journalctl --user -u aerie-ci-poll`.

## Branch protection

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

They mock `gh`, `git` and every subprocess, and run in under a second.
