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
./aerie bench --check zero_traffic              # zero Google/Helium traffic (about 6 minutes)
./aerie bench --check zero_traffic --baseline /path/to/unmodified/helium   # and prove it fails there
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
| `--services MODE` | `browser_requests`: `require`, `optional`, `skip` or `auto` (default). `skip` does not run the probe (use it for Aerie builds). `auto` skips the probe when no `--binary` is given (the default build is Aerie, which makes no requests of its own; decided by the absence of `--binary`, never by name or version), and for an explicit `--binary` requires the seeded services call if it identifies as Helium/Aerie by file name or `--version`, and is optional for other binaries. `--services require` forces it everywhere. |
| `--services-wait SEC` | `browser_requests`: longest wait for the first browser-level request (default 20). It stops early once the condition is met. |
| `--zero-traffic-idle SEC` | `zero_traffic`: length of the idle phase (default 300, the issue's 5 minutes). |
| `--zero-traffic-settle SEC` | `zero_traffic`: quiet time after the first page, the first-launch phase (default 60). |
| `--zero-traffic-strict` | `zero_traffic`: also fail on blocked attempts to `.qjz9zk` dummy hosts and `trk:` URLs. |
| `--widevine-online SEC` | `widevine_opt_in`: also wait up to SEC in an opted-in browser for Widevine to download and become available. It needs a network that reaches Google, so it is off (0) by default. |
| `--baseline PATH` | `zero_traffic`: also run the check on this browser (an unmodified Helium build, or any browser that talks to Google) and require it to fail. If it passes, the check is vacuous and the command exits 1. |
| `--flag FLAG` | Extra browser flag; repeat for several. |
| `--build-root DIR` | Build root (env `AERIE_BUILD_ROOT`, default `/mnt/linux2/aerie-build`). `./aerie bench` only for the flag; `python3 -m aerie_bench` takes it too. |

## What a run does

1. Starts the fixture server on `127.0.0.1`, ephemeral port. It serves
   `tools/aerie_bench/fixtures/` only and logs every request it gets.
2. Creates a fresh `0700` profile under `<build root>/bench-tmp/`.
3. Starts the browser with `--remote-debugging-pipe` (no TCP debug port),
   `--log-net-log` (NetLog), `--no-first-run`, `--password-store=basic`,
   `--no-proxy-server` and `--host-resolver-rules` (see below). Every host
   except loopback fails to resolve. The attempt still appears in the NetLog,
   so external traffic is recorded and refused rather than needed.
4. Runs the checks against the live page.
5. Sends `Browser.close`, waits for the process (terminate, then kill, if it
   hangs), and parses the NetLog.
6. Runs each check's after-exit step, deletes the profile, writes the results.

The resolver rules are `launcher.RESOLVER_RULES`: `*.bench.test` maps to the
loopback fixture server, `localhost`, `*.localhost`, `127.0.0.1`, `127.0.0.2`
and `::1` stay resolvable, everything else is `~NOTFOUND`. `*.localhost` names
are separate sites that HTTPS-only mode does not touch. `*.bench.test` names
are separate sites that HTTPS-only mode does upgrade, which is what the
hardening checks need to prove it is on. The NetLog parser counts both as
loopback, not external.

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
| `seeded_prefs_by` | string[] | Checks that seeded their own profile prefs through `profile_prefs` (for example `zero_traffic`). Added to schema 1 without a bump: existing readers ignore it. |
| `profile.path`, `profile.deleted` | string, bool | The throwaway profile and whether it is gone. |
| `checks[]` | object | `name`, `status` (`pass`/`fail`/`error`/`skip`), `detail`, `duration_s`, `data` (check-specific object). |
| `network.netlog` | string or null | NetLog file name in the results directory. |
| `network.netlog_requests` | int | URL requests in the NetLog. |
| `network.requests[]` | object | Every NetLog URL request: `source_id`, `time`, `url`, `method`, `scheme`, `host`, `initiator` (null for browser-initiated), `net_error` (null or code), `external` (bool, non-loopback). |
| `network.external_requests[]` | object | The `external` subset of `network.requests`. |
| `network.fixture_requests[]` | object | What the fixture server saw: `time`, `method`, `path`, `host`, `status`, `user_agent`, `cookie`, `sec_purpose`, `purpose` (the last three are the request headers of that name, empty when absent). |
| `exit.clean`, `exit.code` | bool, int | `Browser.close` led to exit code 0 within the deadline; the exit code (negative: signal). |
| `status` | string | `pass` when every check is `pass` or `skip`, the profile is deleted and the browser exited; else `fail`. |
| `files` | string[] | Files written to the results directory. |

Results stay in the build root. Committed baselines are issue #16's job.

## Checks

| Name | Passes when |
| --- | --- |
| `smoke` | Browser launches, loads `/index.html`, the title matches, a screenshot is taken, and the browser exits with code 0 after `Browser.close`. |
| `browser_requests` | The browser made at least one request of its own. With services required (default for the build binary and for Helium/Aerie-identified binaries): the fixture server saw a Helium services call (`/bangs.json`, `/ubo/assets.json`, `/com`, `/ext`, `/dict/`). With services optional: that, or a request to a non-loopback host in the NetLog. Waits up to `--services-wait` seconds, less if the condition is met sooner. |
| `zero_traffic` | No URL request in the NetLog goes to a host on the denylist (`docs/network-denylist.md`), in three phases: first launch, 5 minutes idle, and the browse script. Fails on an empty capture. See "Zero traffic" below. |
| `ad_blocking` | The Filter Engine works, including at redirect hops. The page `/blocking/index.html` asks for real ad and tracker URLs (scripts, an image, a subframe, a `fetch`), and for two first-party files. Every URL in `fixtures/blocking/expected.json` under `blocked` must fail with `net::ERR_BLOCKED_BY_CLIENT` (DevTools `loadingFailed`) and must not appear in the NetLog; every `allowed` path must load with status 200 and be seen by the fixture server; the first-party script must run. Two first-party URLs redirect (`fixtures/redirects.json`): `/blocking/go-tracker` to a listed tracker, whose hop must be blocked (the redirecting URL answers 302, the target fails with `ERR_BLOCKED_BY_CLIENT` and never reaches the NetLog), and `/blocking/go-ok.js` to first-party content, which must load. The blocking must be the Aerie engine's: see below. |
| `cosmetic_filtering` | The Filter Engine hides leftover ad containers. The page `/cosmetic/index.html` has boxes with the class names and ids of EasyList's generic hide rules and first-party boxes (one named `adventure`). Its own CSS hides nothing. Every `hidden` selector in `fixtures/cosmetic/expected.json` must have `display: none` and no box; every `visible` one must keep a box. The hide must be the engine's (`Aerie.Adblock.CosmeticRuleCount` has a sample with a sum above zero), and a page script that replaces `CSSStyleSheet` and the `adoptedStyleSheets` accessor must not see the injection (`window.cosmeticHijacked` stays unset). See [adblock.md](adblock.md#cosmetic-filtering-hiding-leftover-ad-containers). |
| `phishing_protection` | The protection category works. A second browser started with `--aerie-test-protection-list` lists fixture `*.localhost` domains; the first browser, without the list, must load the same names (negative control). A listed main-frame navigation (and a subdomain of a listed domain) shows the interstitial: Go back is the primary button, Continue anyway is a link in the closed details section, the fixture server never saw the request, and the site's script and style did not reach the page. Go back returns to the previous page; Continue anyway loads the page, is remembered in that tab and not in a new tab. A listed subframe is blocked, and a control iframe to a clean host loads. A clean URL that answers 302 to a listed host is stopped at the hop, in the main frame and in a subframe. `Aerie.Protection.ListLoadSuccess` must have a true sample and no false one, and `Aerie.Protection.NavigationBlocked` at least eight true samples. See [phishing-protection.md](phishing-protection.md). |
| `mv2_extension` | Manifest V2 works and uBlock Origin is gone. A tiny MV2 extension (`tools/aerie_bench/extensions/mv2`) is installed with `Extensions.loadUnpacked` (the Bench's pipe session is a browser-target session, which is what allows that command; no launch flag is needed), its persistent background page must start, its content script must run on `/mv2/index.html`, its blocking `webRequest` listener must cancel `/mv2/ext-block-me.js` (`ERR_BLOCKED_BY_CLIENT`) and leave `/mv2/allowed.js` alone, and the extension itself must say it cancelled it (its background page records every cancel and the check reads that list, so the Aerie engine cannot be what blocked it), and no DevTools target may exist under the bundled uBlock Origin's component id. |
| `filter_lists` | Users manage filter lists and can turn blocking off per site (issue #10). Runs in a browser started with `--aerie-test-allow-loopback-filter-lists` and drives `chrome://filters` through `chrome.send`. Passes when: an `http://` list on a non-loopback host and a list that is an HTML page (`fixtures/filters/not-a-list.html`) both show an error and block nothing; adding the fixture list URL (`fixtures/filters/custom-list.txt`) blocks the page's three ads at once, without a restart, while first-party content loads; `Aerie.Adblock.PageBlockedCount` has one sample equal to the three blocked ads; a forced update (`aerieFilters.updateAll`) fetches the custom list again and every non-loopback request in the NetLog is an https request to a list publisher's own URL (easylist.to for the built-in lists, whose update fails in the Bench, which is tolerated); an exception for `news.localhost` lets the ads load there and not on `other.localhost`; and after the browser restarts on the same profile the list and the exception still apply. |
| `widevine_opt_in` | Widevine is opt-in (#7). On a fresh profile `requestMediaKeySystemAccess('com.widevine.alpha')` is refused, chrome://components does not list the Widevine component, and the NetLog has no request that could fetch it (a real host, or any URL naming Widevine). Negative control: a second browser whose Local State has `aerie.widevine.enabled` must list the component, so the reader can see Widevine. With `--widevine-online SEC` that browser must also answer the key system query with success within SEC seconds (the download reaches Google). Not in the default scenario; run `./aerie bench --check widevine_opt_in`. |

`browser_requests` is a self-test for the Bench and for Helium. Aerie makes no
requests of its own, so a plain `./aerie bench` (no `--binary`, the default
Aerie build) skips it, prints one line saying so, and records the same text in
the check's `data.skipped_by` and `data.needs_baseline` in `results.json`. The
skip comes from the absence of `--binary`, never from the binary's name or
`--version`. `--services require` forces the probe. With an explicit `--binary`
that is an Aerie build, pass `--services skip`.

**Known gap.** On Aerie there is no browser-initiated request for the Bench to
see, so a plain Aerie run does not prove that the NetLog would show one. That
proof comes from `--baseline` (the same check fails on a browser that does talk
to Google or Helium). A better canary is possible: an https fixture on
127.0.0.1 configured as the extension proxy with
`--aerie-test-allow-loopback-extension-proxy`, plus a trigger that makes the
browser call it. It needs a certificate and a trigger that works with no
extension installed (`chrome.developerPrivate.autoUpdate` sends nothing then),
so it is left to a follow-up. Until then, the loopback switch and the
validator are covered only by the C++ unit tests.

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

### Zero traffic

`zero_traffic` is the proof of the M1 promise. One run, three phases:

1. `first_launch`: fresh profile, first fixture page, then `--zero-traffic-settle` seconds (60).
2. `idle`: `--zero-traffic-idle` seconds (300) of nothing. Timers fire here.
3. `browse`: the fixture script (`index.html`, a click, `browse-a.html`,
   `browse-b.html`) and a best-effort update request from `chrome://extensions`.

The profile is seeded as the worst case: the user consented to Helium services
and turned every service on, with no origin override. Unmodified Helium then
calls `services.helium.imput.net`. Aerie must stay silent anyway, because its
code has no origin to call.

The verdict comes from the NetLog after the browser exits. It fails when:

- any request matches the denylist (Google and Helium/imput hosts), or any host name in a resolver,
  connect or socket event does (DNS prefetch, preconnect, DoH). The Bench's own resolver rules rewrite
  names to `~notfound` before some of those events are logged, so the host-name scan is a second net,
  not the main one;
- the capture holds no fixture page request at all (an empty capture proves nothing);
- (inconclusive) the NetLog is truncated or missing, or the browser did not exit with code 0.
  A crash or kill loses the tail of the log, which may hold the traffic that matters;
- with `--zero-traffic-strict`, any request goes to a `.qjz9zk` dummy host or a
  `trk:` URL. These are refused inside the network stack before any DNS query,
  so the default run lists them as `blocked` and still passes.

`checks[].data` has `hits` (url, host, group, pattern, phase, net_error),
`blocked`, `other_hosts` (external hosts that are not on the list, for review),
`phases` (cumulative request count at the end of each phase), `fixture_requests`
and `extension_update_probe_error`.

To see the check fail, run it on a browser that is not Aerie:

```sh
# the quickest demonstration; any Chromium that contacts Google fails within seconds
./aerie bench --binary /path/to/chrome --check zero_traffic --zero-traffic-settle 5 --zero-traffic-idle 20
# the acceptance proof: Aerie passes, and the unmodified Helium build must fail
./aerie bench --check zero_traffic --baseline /path/to/unmodified/helium
```

With `--baseline` the command exits 0 only if the first browser passes and the
baseline fails. A baseline that passes means the check cannot see traffic.

Limits: the NetLog covers URL requests (browser, extensions, workers). It does
not cover a separate process that makes its own sockets, such as the crash
handler. Crash upload is removed in the source and checked by the source gates
(`docs/network-endpoints.md`).

### Branding checks

Three checks for the rebrand (issue #5). Run them with
`./aerie bench --scenario tools/aerie_bench/scenarios/branding.json`. They read
internal pages over DevTools, so they need a real build. Each says in
`negative_control` how it proves that it can fail. Their judging code is tested
against recorded observations in `tests/test_bench_branding.py`.

| Name | Passes when | Negative control |
| --- | --- | --- |
| `branding_no_helium` | The title and the text (including every shadow root, and the `aria-label`, `title`, `placeholder`, `alt` and `label` attributes) of 22 internal pages never contain the whole word Helium, in any case: About (`chrome://version`, `chrome://settings/help`), the settings pages, `chrome://flags`, the chrome-urls list, history, downloads, extensions, bookmarks and the others in `PAGES` (`tools/aerie_bench/checks/branding.py`). A page that does not load fails the check. | A fixture page with the word inside a shadow root is read by the same scanner and must be reported. |
| `branding_scheme` | A tab opened on `aerie://version` shows the version page, and one opened on `helium://version` does not (it ends on `about:blank` or an error page, like an unknown scheme). | A tab on the made-up scheme `zzz://version` must not load the version page either. |
| `branding_version` | About and `chrome://version` both show the Aerie release (`AERIE_VERSION`) and the Chromium version (`helium.lock`), and neither shows Helium's own version number. | The Helium version number must be absent, so a build that still shows it fails even with the other two present. |

Until issue #23 removes the Helium services settings and the onboarding
consent, the text of `chrome://settings/privacy`, `chrome://settings/privacy/services`,
and `chrome://settings/languages` may still name Helium services (`chrome://setup` is Aerie's own page since #46 and is checked strictly). `branding_no_helium`
scans those pages, reports how many hits each has in its detail line, and fails
only if their title says Helium. Delete `PENDING_SERVICES_PAGES` when #23 lands.

Three other exemptions are each limited to one page. On `chrome://version`, the
Command Line, Executable Path and Profile Path lines (build directory names)
and the exact string "The Helium Authors" are ignored: the copyright line
credits Aerie, Helium and Chromium, because Aerie is derived from both. On
`chrome://flags`, internal flag ids such as `#helium-noise` are ignored. The same
text on any other page fails the check.

### Hardening checks

One check for each control in `docs/hardening.md`, plus three that cover the
whole set. They drive pages under `tools/aerie_bench/fixtures/hardening/`. Run
them all with `./aerie bench --scenario tools/aerie_bench/scenarios/hardening.json`
or one at a time with `--check hardening_https_only`. Each check uses its own host names, so an exception one check creates cannot change another. Each check says in `negative_control`
how it proves that it can fail, and runs that control for real in the same
Bench run, in the same browser or in a variant (see below).

| Name | Control | Passes when | Negative control |
| --- | --- | --- | --- |
| `hardening_https_only` | HTTPS-only mode | An http page on a non-exempt name (`plain.bench.test`) shows the HTTPS-only warning page with a visible Continue to site button, and the fixture server sees no request before the click. | After the click the page loads over http and the server sees it. |
| `hardening_localhost_dev` | HTTPS-only mode | `http://localhost:<port>`, `dev.localhost` and `127.0.0.1` load with no warning page, and `127.0.0.2` and `::1` are not upgraded to https. | `strict.bench.test` gets the warning page in the same run. |
| `hardening_http_exception` | HTTPS-only mode | One click allows a host over http, it is listed under Sites allowed over HTTP in settings, another host stays blocked, and Remove brings the warning page back. | The host is not on the list and is blocked before the click. |
| `hardening_third_party_cookies` | Third-party cookies | A frame from another site reads no cookie that its own site set. | A variant with third-party cookies allowed reads it. |
| `hardening_storage_partitioning` | Partitioned storage | A frame stores a value under site a and sees none under site c. | A variant with `--disable-features=ThirdPartyStoragePartitioning` sees it under both. |
| `hardening_webrtc` | WebRTC IP leak protection | ICE gathering (with a loopback STUN server) finishes, produces at least one candidate, and no candidate has a local address or an mDNS name. | A variant with the `default` policy and mDNS hiding off exposes a raw local address. |
| `hardening_prefetch` | No prefetch | No request reaches a hinted URL (`rel=prefetch`, `prerender`, speculation rules) and the TCP listener sees no preconnect. | The page's own control fetch and control connection are seen, and a variant with preloading on shows at least one speculative load. |
| `hardening_site_isolation` | Strict site isolation | The cross-site frame has its own iframe target. | A variant with `--disable-site-isolation-trials` has none. |
| `hardening_fingerprinting` | Fingerprinting randomization | The canvas hash is stable for one site in one session. Canvas and audio hashes differ across sites and across sessions, and the audio hash differs from the no-noise control. Audio is not required to be stable within a session: Helium salts its noise with a per-render counter, so every render differs. That is accepted for now (issue #38). | A variant with `--disable-features=HeliumNoise` gives equal hashes on both sites. |
| `hardening_js_optimizer` | JavaScript JIT per site | The JavaScript optimization page loads and its default is on. The JIT is turned off for site a through the Site settings handler (`setOriginPermissions`). Then a hot loop (`hardening/jit.html`) runs at least 2x slower on a than on b, also after a clean restart on the same profile; when one page frames the other site, in either order, the two run at different speeds (so in different processes); a renderer under the browser has `--disable-optimizing-compilers` in its `--js-flags`, and not every renderer has. Site b is not slowed. | A second browser that never changes the setting: a and b run at the same speed and no renderer lacks the optimizing compilers. |
| `hardening_extension_prefs` | Extension lock | An extension with the privacy permission is refused when it writes the WebRTC policy, the preloading option or the third-party cookie setting. | The same extension changes `referrersEnabled`. |
| `hardening_pref_defaults` | all | The live pref values (read through `chrome.settingsPrivate` on the settings page) equal the defaults in `controls.json`. | A variant that seeds a different WebRTC policy is reported as a mismatch. |
| `hardening_settings_page` | all | `chrome://settings/privacy/hardening` has a row for every control with its default and cost text, and the live controls show the default. | A control id that is not in the table is absent. |
| `hardening_tamper` | all | A page with a user gesture that opens `chrome://settings` or `chrome://flags`, asks for storage access and probes cookies changes no hardened default. | An ordinary popup does open. |

Notes:

- The checks use `chrome://` URLs. If the scheme is renamed (issue #5), change
  `SETTINGS_URL` and `SETTINGS_ROOT` in `checks/hardening/`.
- DNS prefetch has no observable effect when every name resolves locally, so
  `hardening_pref_defaults` covers it through the preloading pref.
- A check that cannot show its own negative control fails. It never passes
  because nothing was seen.
- `tests/fixtures/hardening/*.json` hold the observations that `judge()` is
  tested against. They were written from what each browser feature is meant to
  do, before a hardened build existed. Replace them with observations from a
  real run (the `data` field of each check in `results.json`) after the first
  train build, and keep the failing cases.

### Variants and probes

`session.variant(extra_flags, prefs)` starts a second browser for a negative
control: a fresh throwaway profile with `prefs` seeded into
`Default/Preferences`, the main run's flags plus `extra_flags`, the same
fixture server. It is stopped and its profile deleted when the block ends, also
after an error. Variant browser logs are `browser-stderr-variant-<n>.log`.
To restart a browser on the same profile, make one with `with session.new_profile() as p:` and
pass it as `session.variant(flags, profile=p)` for each launch (the profile is deleted when the
`new_profile` block ends). After a variant block, its `netlog` holds that launch's NetLog records.
Variant sessions have `browser_pid`; `procmem.renderer_modes(pid)` reads
the renderers under it from `/proc` and says which run without the optimizing compilers.
`session.stun_probe()` is a loopback STUN server (so ICE gathering has a server). `session.tcp_probe()` is a loopback listener that counts connections (to see
preconnects). Other helpers: `session.navigate_url(url)` (a failed navigation
is returned as `{"error": ...}`), `session.wait_for(js)`, `session.targets()`,
and `evaluate(js, await_promise=True, user_gesture=True)`.

The fixture server also serves `/hardening/echo` (the request's cookie and
host) and `/hardening/set-cookie` (a `SameSite=None; Secure` cookie), and each
entry in `network.fixture_requests` now has `cookie`, `sec_purpose` and
`purpose`.

### The `ad_blocking` check

**Status:** Helium's bundled uBlock Origin is removed (patches dropped, asset download skipped; see
[helium-dependency-ledger.md](helium-dependency-ledger.md)), so on a real Aerie build this check can pass and
credit the blocking to the Aerie engine. The `mv2_extension` check proves the other half: Manifest V2 still works.
On a build that still has uBlock Origin, or on stock Chromium or Helium, it fails, which is the control.

The Bench resolves nothing but loopback, so an ad URL that is not blocked fails with
`net::ERR_NAME_NOT_RESOLVED` and shows in the NetLog, while a blocked one fails with
`ERR_BLOCKED_BY_CLIENT` and never reaches the network stack. The check reads that
difference. Stock Chromium and Helium (without the engine) fail it, which is the point;
this is the control. No traffic leaves the machine either way.

A build that still shipped uBlock Origin would block the same URLs with the same error. So a pass also needs the engine's own signals, read through DevTools
`Browser.getHistogram`: `Aerie.Adblock.EngineLoadSuccess` with a true sample and no false
one, and at least as many true `Aerie.Adblock.RequestBlocked` samples as the page has
expected-blocked URLs. A request that uBlock Origin stops never reaches the engine.

The page names real hosts, so it is the one fixture that references external URLs. A test
(`tests/test_bench_ad_blocking.py`) keeps that to `fixtures/blocking/` and to the hosts in
`expected.json`. The URLs are long-lived rules of big ad and analytics hosts. If a list
update ever unblocks one, the check fails and says which.

`checks[].data` also records, with no threshold:

| Field | Meaning |
| --- | --- |
| `metrics.page_timing` | `dom_content_loaded_ms`, `load_event_ms`, `response_end_ms` of the fixture page (Navigation Timing). |
| `metrics.resource_entries` | Number of resource timing entries. |
| `metrics.memory` | `total_kb` and a per-process list: resident memory of the browser's whole process tree (Linux, from `/proc`). |
| `metrics.engine_load` | Count, sum and mean in ms of the histogram `Aerie.Adblock.EngineLoadTime` (`null` if the browser has no such histogram). |

The check writes `ad_blocking-recording.json` (the DevTools events, the NetLog URLs, the
fixture server paths) to the results directory. The tests judge such recordings
(`tests/data/ad_blocking/`); to refresh them from a real run, copy a passing recording over
`pass.json`.

### Writing a check

A check is a class in `tools/aerie_bench/checks/` registered with
`@register`:

```python
@register
class MyCheck:
    name = "my_check"
    seeds_helium_services = False          # True: seed the services prefs, as above

    def profile_prefs(self, server):       # optional: prefs merged into Default/Preferences
        return {}

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
