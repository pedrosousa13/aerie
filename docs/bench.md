# The Aerie Bench

The Bench launches a built browser with a throwaway profile, drives it over the
DevTools Protocol, serves local fixture pages, and records every network
request the browser makes, page requests and browser-initiated ones alike. It
writes machine-readable JSON and a human summary. It is the test seam that the
zero-Google-traffic proof, the hardening checks, the CI gate and the
performance metrics build on.

Python 3.11+, standard library only. No Playwright, no Node, no network.

## Run it

```sh
./aerie bench                                   # the browser ./aerie build produced; smoke check
./aerie bench --binary /path/to/chrome          # any Chromium-family binary
./aerie bench --binary /path/to/chrome --check smoke --check browser_requests
./aerie bench --scenario tools/aerie_bench/scenarios/click-through.json
python3 -m aerie_bench --binary /path/to/chrome # same thing without ./aerie
```

Exit code: `0` all checks passed, `1` a check failed or errored, `2` the
harness itself failed (bad binary, bad scenario, unknown check).

Options:

| Flag | Meaning |
| --- | --- |
| `--binary PATH` | Browser to test. Default for `./aerie bench`: the Helium/Aerie binary in the build root. Required for `python3 -m aerie_bench`. |
| `--check NAME` | Run this check; repeat for several. Default: `smoke`. |
| `--scenario FILE` | JSON scenario (see below). `--check` overrides its `checks` list. |
| `--headed` | Show the window. Default is `--headless=new`. |
| `--no-sandbox` | Pass `--no-sandbox`. Containers only; recorded in the results. See "Sandbox" below. |
| `--out DIR` | Results directory. Default `<build root>/bench/<UTC timestamp>/`. |
| `--timeout SEC` | Timeout for each CDP command (default 30). |
| `--services MODE` | `browser_requests`: `require`, `optional` or `auto` (default). `auto` requires the seeded services call for the default build binary (never guessed by name), and for an explicit `--binary` that identifies as Helium/Aerie by file name or `--version`; it is optional for other explicit binaries. |
| `--services-wait SEC` | `browser_requests`: longest wait for the first browser-level request (default 20). It stops early once the condition is met. |
| `--flag FLAG` | Extra browser flag; repeat for several. |
| `--build-root DIR` | Build root (env `AERIE_BUILD_ROOT`, default `/mnt/linux2/aerie-build`). `./aerie bench` only for the flag; `python3 -m aerie_bench` takes it too. |

## What a run does

1. Starts the fixture server on `127.0.0.1`, ephemeral port. It serves
   `tools/aerie_bench/fixtures/` only and logs every request it gets.
2. Creates a fresh `0700` profile under `<build root>/bench-tmp/`.
3. Starts the browser with `--remote-debugging-pipe` (no TCP debug port),
   `--log-net-log` (NetLog), `--no-first-run`, `--password-store=basic`,
   `--no-proxy-server` and `--host-resolver-rules="MAP * ~NOTFOUND , EXCLUDE
   127.0.0.1"`. Every host except loopback fails to resolve. The attempt still
   appears in the NetLog, so external traffic is recorded and refused rather
   than needed.
4. Runs the checks against the live page.
5. Sends `Browser.close`, waits for the process (terminate, then kill, if it
   hangs), and parses the NetLog.
6. Runs each check's after-exit step, deletes the profile, writes the results.

The browser gets an allowlisted environment (`PATH`, `LANG`, `LC_*`, `TZ`,
`DISPLAY`, `WAYLAND_DISPLAY`, `XAUTHORITY`, `XDG_RUNTIME_DIR` and similar), with
`HOME`, `XDG_*_HOME` and `TMPDIR` inside the throwaway profile. Tokens from
the calling shell do not reach it. On timeout the whole process group is
terminated, then killed, under one shared deadline.

The Bench never passes `--disable-background-networking`,
`--disable-component-update` or `--disable-sync`: they would hide the traffic
the later checks must audit.

## Results

Under `<build root>/bench/<UTC timestamp>/` (mode `0700`):

| File | Content |
| --- | --- |
| `results.json` | Schema 1. Binary and version, flags, per-check status and detail, `network.requests` (every URL request from the NetLog: url, method, initiator, host, `external`, `net_error`), `network.fixture_requests`, `network.external_requests`, exit info, `profile.deleted`, overall `status`. |
| `summary.txt` | The same, for people. Also printed by the command. |
| `netlog.json` | Raw Chromium NetLog. Can hold request headers; do not upload it as a CI artifact without review. |
| `smoke.png`, other `*.png` | Screenshots. |
| `browser-stderr.log` | The browser's stderr; first place to look when launch fails. |

### results.json, schema 1

| Field | Type | Meaning |
| --- | --- | --- |
| `schema` | int | Always `1`. Bumped on any breaking change. |
| `scenario` | string | Scenario name (`default` without `--scenario`). |
| `binary.path`, `binary.version` | string, string or null | Browser tested and its `--version` output. |
| `started`, `duration_s` | ISO 8601 UTC string, number | Start time and wall time. |
| `flags` | string[] | Browser command line after the binary (includes the profile path). |
| `headless`, `no_sandbox` | bool | Mode switches used. |
| `seeded_helium_services` | bool | Whether the profile prefs were seeded for the services probe. |
| `profile.path`, `profile.deleted` | string, bool | The throwaway profile and whether it is gone. |
| `checks[]` | object | `name`, `status` (`pass`/`fail`/`error`/`skip`), `detail`, `duration_s`, `data` (check-specific object). |
| `network.netlog` | string or null | NetLog file name in the results directory. |
| `network.netlog_requests` | int | URL requests in the NetLog. |
| `network.requests[]` | object | Every NetLog URL request: `source_id`, `time`, `url`, `method`, `scheme`, `host`, `initiator` (null for browser-initiated), `net_error` (null or code), `external` (bool, non-loopback). |
| `network.external_requests[]` | object | The `external` subset of `network.requests`. |
| `network.fixture_requests[]` | object | What the fixture server saw: `time`, `method`, `path`, `host`, `status`, `user_agent`. |
| `exit.clean`, `exit.code` | bool, int | `Browser.close` led to exit code 0 within the deadline; the exit code (negative: signal). |
| `status` | string | `pass` when every check is `pass` or `skip`, the profile is deleted and the browser exited; else `fail`. |
| `files` | string[] | Files written to the results directory. |

Results stay in the build root. Committed baselines are issue #16's job.

## Checks

| Name | Passes when |
| --- | --- |
| `smoke` | Browser launches, loads `/index.html`, the title matches, a screenshot is taken, and the browser exits with code 0 after `Browser.close`. |
| `browser_requests` | The browser made at least one request of its own. With services required (default for the build binary and for Helium/Aerie-identified binaries): the fixture server saw a Helium services call (`/bangs.json`, `/ubo/assets.json`, `/com`, `/ext`, `/dict/`). With services optional: that, or a request to a non-loopback host in the NetLog. Waits up to `--services-wait` seconds, less if the condition is met sooner. |

`browser_requests` seeds the profile's `Default/Preferences` with
`helium.services.origin_override = http://127.0.0.1:<port>` (plus
`schema_version: 1`, `enabled: true`) before the first launch. Helium accepts
only https or localhost overrides, so its service calls land on the fixture
server. A browser without Helium services (stock Chromium) passes through the
NetLog path instead: its Google traffic is recorded and refused. On a
Helium/Aerie binary a silent services probe is a failure, so a regression
cannot hide behind other traffic. The check records `passed_via`
(`services`, `netlog` or null), `services_hits`, `external_hosts` and
`require_services` in `checks[].data`.

### Writing a check

A check is a class in `tools/aerie_bench/checks/` registered with
`@register`:

```python
@register
class MyCheck:
    name = "my_check"
    seeds_helium_services = False          # True: seed the services prefs, as above

    def run(self, session):                # while the browser is live
        session.navigate("/index.html")
        return CheckResult(self.name, "pass", "...")

    def finalize(self, session, result):   # after exit; session.netlog, session.exit are set
        return result
```

Both methods are optional. `Session` offers `navigate(path)`, `evaluate(js)`,
`click(selector)`, `screenshot(name)`, `fixtures.snapshot()`, `client` (raw
CDP) and, after exit, `netlog` and `exit`. Status is `pass`, `fail`, `error`
(an exception) or `skip`. Programmatic entry point:
`aerie_bench.runner.run_bench(binary, scenario, build_root, ...)`.

## Scenarios

```json
{
  "name": "click-through",
  "checks": ["smoke"],
  "steps": [
    {"navigate": "/index.html"},
    {"click": "#go"},
    {"evaluate": "document.getElementById('heading').textContent", "expect": "clicked"},
    {"screenshot": "after-click.png"},
    {"wait_ms": 100}
  ]
}
```

`navigate` takes a fixture path, never a URL. `screenshot` takes a plain `.png`
file name. A scenario with `steps` and no `checks` runs only the steps.

## Sandbox

Chromium's sandbox needs unprivileged user namespaces. On Ubuntu 23.10 and
later, AppArmor restricts them, and an unpacked browser binary aborts with "No
usable sandbox" (the Bench saves this in `browser-stderr.log`). Either install
an AppArmor profile for the binary or pass `--no-sandbox`. The Bench never adds
it on its own.

## Offline and security

- The fixture server binds `127.0.0.1` only; a test checks this.
- Nothing needs credentials or the network. Fixtures reference nothing
  external (a test scans them).
- The CDP channel is a pipe, not a TCP port, so other local users cannot attach.
- The profile is deleted after every run, including after errors.

## Tests

```sh
python3 -m unittest discover -s tests -t . -v
```

The Bench tests run against a fake browser (`tests/bench_fake_browser.py`) that
speaks the pipe protocol. No real browser is needed.
