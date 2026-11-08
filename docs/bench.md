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
| `--workspaces-enforce` | `workspace_isolation`: judge every probe for real. Without it the probes in `EXPECTED_TO_FAIL` (all of them until Workspaces ship) may fail and the check reports `xfail`. See "Workspace isolation" below. |
| `--baseline PATH` | `zero_traffic`: also run the check on this browser (an unmodified Helium build, or any browser that talks to Google) and require it to fail. If it passes, the check is vacuous and the command exits 1. |
| `--flag FLAG` | Extra browser flag; repeat for several. |
| `--build-root DIR` | Build root (env `AERIE_BUILD_ROOT`, default `/mnt/linux2/aerie-build`). `./aerie bench` only for the flag; `python3 -m aerie_bench` takes it too. |
| `--lock-timeout SEC` | `./aerie bench` only. How long to wait for the build-tree lock (default: wait; `0` fails at once). See [ci.md](ci.md). |

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
| `checks[]` | object | `name`, `status` (`pass`/`fail`/`error`/`skip`, and `xfail`/`xpass` for a check written before its feature), `detail`, `duration_s`, `data` (check-specific object). |
| `network.netlog` | string or null | NetLog file name in the results directory. |
| `network.netlog_requests` | int | URL requests in the NetLog. |
| `network.requests[]` | object | Every NetLog URL request: `source_id`, `time`, `url`, `method`, `scheme`, `host`, `initiator` (null for browser-initiated), `net_error` (null or code), `external` (bool, non-loopback). |
| `network.external_requests[]` | object | The `external` subset of `network.requests`. |
| `network.fixture_requests[]` | object | What the fixture server saw: `time`, `method`, `path`, `host`, `status`, `user_agent`, `cookie`, `authorization`, `sec_purpose`, `purpose` (the last three are the request headers of that name, empty when absent). |
| `exit.clean`, `exit.code` | bool, int | `Browser.close` led to exit code 0 within the deadline; the exit code (negative: signal). |
| `status` | string | `pass` when every check is `pass`, `skip` or `xfail`, the profile is deleted and the browser exited; else `fail`. |
| `files` | string[] | Files written to the results directory. |

Results stay in the build root. Committed baselines are issue #16's job.

## Checks

| Name | Passes when |
| --- | --- |
| `smoke` | Browser launches, loads `/index.html`, the title matches, a screenshot is taken, and the browser exits with code 0 after `Browser.close`. |
| `browser_requests` | The browser made at least one request of its own. With services required (default for the build binary and for Helium/Aerie-identified binaries): the fixture server saw a Helium services call (`/bangs.json`, `/ubo/assets.json`, `/com`, `/ext`, `/dict/`). With services optional: that, or a request to a non-loopback host in the NetLog. Waits up to `--services-wait` seconds, less if the condition is met sooner. |
| `zero_traffic` | No URL request in the NetLog goes to a host on the denylist (`docs/network-denylist.md`), in three phases: first launch, 5 minutes idle, and the browse script. Fails on an empty capture. See "Zero traffic" below. |
| `ad_blocking` | The Filter Engine works, including at redirect hops. The page `/blocking/index.html` asks for real ad and tracker URLs (scripts, an image, a subframe, a `fetch`), and for two first-party files. Every URL in `fixtures/blocking/expected.json` under `blocked` must fail with `net::ERR_BLOCKED_BY_CLIENT` (DevTools `loadingFailed`) and must not appear in the NetLog; every `allowed` path must load with status 200 and be seen by the fixture server; the first-party script must run. Two first-party URLs redirect (`fixtures/redirects.json`): `/blocking/go-tracker` to a listed tracker, whose hop must be blocked (the redirecting URL answers 302, the target fails with `ERR_BLOCKED_BY_CLIENT` and never reaches the NetLog), and `/blocking/go-ok.js` to first-party content, which must load. The blocking must be the Aerie engine's: see below. |
| `cosmetic_filtering` | The Filter Engine hides leftover ad containers, and a page cannot undo it. The page `/cosmetic/index.html` has boxes with the class names and ids of EasyList's generic hide rules and first-party boxes (one named `adventure`). Its own CSS hides nothing. Every `hidden` selector in `fixtures/cosmetic/expected.json` must have `display: none` and no box; every `visible` one must keep a box. The hide must be the engine's (`Aerie.Adblock.CosmeticRuleCount` has a sample with a sum above zero), and a page script that replaces `CSSStyleSheet` and the `adoptedStyleSheets` accessor must not see the injection (`window.cosmeticHijacked` stays unset). Two more cases prove ADR 0005 (issue #68). **Attacked** (the four boxes in the markup): 3 s after load the page empties `adoptedStyleSheets` through the real setter, removes its `<style>` elements and its `<link>` to `page.css`, and forces `display:block !important` inline on the boxes; they must still be hidden, because the hide is a user-origin sheet. **Late** (`.advert-box`, and `#adContainer` inside a wrapper): the page inserts them 3.5 s after load, long after the browser's load-time passes read its class names and ids; they must be hidden too. **Late class** (`.sponsored-links`): the page adds that class to an element that was already in the markup (as `promo`), also 3.5 s after load; it must be hidden. The page sets `window.cosmeticAttackDone` and `window.cosmeticLateInserted` when it has done both, and a run where either is unset fails (it proves nothing). The check waits for both, lets the hide settle, waits again and measures twice. The frame must have exactly one generic hide sheet. A user-origin sheet is not in `adoptedStyleSheets`, so the check counts it through the CSS domain: `CSS.getMatchedStylesForNode` on the `generic_sheet_probe` element (`.ad-slot`) lists the matching rules with their origin. Blink reports a user-origin rule as `user-agent` (user rules are matched before any tree scope, so the rule's CSSOM wrapper has no parent sheet and gets the inspector's default UA sheet), and the real UA sheet has no rule for `.ad-slot`, so the check counts the `user-agent` rules whose selector is exactly `.ad-slot`, and there must be one (`count_probe_rules` in `tools/aerie_bench/checks/cosmetic_filtering.py`; its tests record the reply shape). See [adblock.md](adblock.md#cosmetic-filtering-hiding-leftover-ad-containers). |
| `phishing_protection` | The protection category works. A second browser started with `--aerie-test-protection-list` lists fixture `*.localhost` domains; the first browser, without the list, must load the same names (negative control). A listed main-frame navigation (and a subdomain of a listed domain) shows the interstitial: Go back is the primary button, Continue anyway is a link in the closed details section, the fixture server never saw the request, and the site's script and style did not reach the page. Go back returns to the previous page; Continue anyway loads the page, is remembered in that tab and not in a new tab. A listed subframe is blocked, and a control iframe to a clean host loads. A clean URL that answers 302 to a listed host is stopped at the hop, in the main frame and in a subframe. `Aerie.Protection.ListLoadSuccess` must have a true sample and no false one, and `Aerie.Protection.NavigationBlocked` at least eight true samples. See [phishing-protection.md](phishing-protection.md). |
| `mv2_extension` | Manifest V2 works and uBlock Origin is gone. A tiny MV2 extension (`tools/aerie_bench/extensions/mv2`) is installed with `Extensions.loadUnpacked` (the Bench's pipe session is a browser-target session, which is what allows that command; no launch flag is needed), its persistent background page must start and its listener must be live (the check fetches a probe URL from the background page until the extension cancels it, and only then loads the page; see "MV2 extensions and the hook" in adblock.md), its content script must run on `/mv2/index.html`, its blocking `webRequest` listener must cancel `/mv2/ext-block-me.js` (`ERR_BLOCKED_BY_CLIENT`) and leave `/mv2/allowed.js` alone, and the extension itself must say it cancelled it (its background page records every cancel and the check reads that list, so the Aerie engine cannot be what blocked it), and no DevTools target may exist under the bundled uBlock Origin's component id. |
| `filter_lists` | Users manage filter lists and can turn blocking off per site (issue #10). Runs in a browser started with `--aerie-test-allow-loopback-filter-lists` and drives `chrome://filters` through `chrome.send`. Passes when: an `http://` list on a non-loopback host and a list that is an HTML page (`fixtures/filters/not-a-list.html`) both show an error and block nothing; adding the fixture list URL (`fixtures/filters/custom-list.txt`) blocks the page's three ads at once, without a restart, while first-party content loads; `Aerie.Adblock.PageBlockedCount` has one sample equal to the three blocked ads; a forced update (`aerieFilters.updateAll`) fetches the custom list again and every non-loopback request in the NetLog is an https request to a list publisher's own URL (easylist.to for the built-in lists, whose update fails in the Bench, which is tolerated); an exception for `news.localhost` lets the ads load there and not on `other.localhost`; after the browser restarts on the same profile the list and the exception still apply; and in an incognito window an exception for `incognito.localhost` lets the ads load there only, is gone from the regular window's list and from the state file, cannot undo an exception saved in the regular profile, and is gone again once the incognito window closes. |
| `workspace_isolation` | Nothing crosses between two Workspaces (issue #73, ADR 0006). Written before the feature: until Workspaces exist every probe is expected to fail, the check reports `xfail` and the run stays green. See "Workspace isolation" below. Not in the default scenario; run `./aerie bench --check workspace_isolation`. |
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

Three exemptions are each limited to one page. On `chrome://version`, the
Command Line, Executable Path and Profile Path lines (build directory names)
and the exact string "The Helium Authors" are ignored: the copyright line
credits Aerie, Helium and Chromium, because Aerie is derived from both. On
`chrome://flags`, internal flag ids such as `#helium-noise` are ignored. The same
text on any other page fails the check. That includes `chrome://settings/privacy`,
`chrome://settings/languages` and the old Helium services link
(`chrome://settings/privacy/services`, which now opens the Settings home page):
the services page is gone (issue #23).

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
| `hardening_fingerprinting` | Fingerprinting randomization | The canvas and audio hashes are each stable for one site in one session, also across two renders in one page. They differ across sites and across sessions, and both differ from the no-noise control. | A variant with `--disable-features=HeliumNoise` gives equal hashes on both sites. |
| `hardening_fingerprinting_gpu` | Fingerprinting randomization, graphics card | With protection on, the WebGL unmasked vendor and renderer are the family strings (for example `Google Inc. (AMD)`), with no digit beyond the GL ES version, on the page and in a worker. WebGPU adapter info is empty, or there is no adapter (the result says which). | Three more browsers: `--disable-features=HeliumNoiseWebGL`, protection off for the whole browser and protection off for `a.localhost` must each show the real strings, and `b.localhost` must stay masked in the last one. |
| `hardening_fingerprinting_site` | Fingerprinting randomization | Canvas, audio and core count are equal on `x.a.localhost`, `y.a.localhost` and `a.localhost` (one site), and canvas and audio differ from `b.localhost`. Two incognito contexts differ from the regular profile and from each other. Turning protection off for `http://[*.]a.localhost` through the Site settings handler of a regular profile restores the real values on both hosts of the site, not on `b`. The regular profile's values are unchanged after the incognito contexts. (The off switch of an off-the-record profile is not driven here, because `chrome://settings` does not open off the record; `aerie_fingerprint_unittests` covers it.) An old origin-scoped exception and an `http://[*.]a.localhost` exception, both seeded in the profile, cover the whole site. | A variant with `--disable-features=HeliumNoise` gives the real, equal values on both sites; they are what turning protection off must restore. The equal-values step is the one that fails on origin keying. |
| `hardening_device_memory` | Fingerprinting randomization | `navigator.deviceMemory` in a window and in a worker, and the `Sec-CH-Device-Memory` and legacy `Device-Memory` headers the fixture server sees after an `Accept-CH` opt-in (`hardening/device-memory.html`), are at most 8. The same holds in a second browser with the `helium.noise.enabled` pref off and in a third with a per-site `helium_noise` (HELIUM_NOISE) block exception for the fixture origin. On a machine that reports 8 GB or less with the cap off the check is inconclusive (status `error`), never a pass. | A variant with `--disable-features=HeliumNoise` reports the machine's own bucket in the window, the worker and both headers. |
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
`with session.incognito() as inc:` opens a page in a new off-the-record browser context of the same browser (CDP `Target.createBrowserContext`) and disposes it when the block ends, also after an error; every call is a fresh context with its own seeds and in-memory settings. `inc` has the session methods (`navigate_url`, `evaluate`, ...). Variant sessions have `browser_pid`; `procmem.renderer_modes(pid)` reads
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

### Workspace isolation

`workspace_isolation` is the proof that M5 has to satisfy ([ADR 0006](adr/0006-workspace-isolation.md),
[threat model](security/workspaces-threat-model.md)). It was written before the feature, like a test before the
code. It runs the same shape of probe for each kind of state:

1. Write a unique token in Workspace X, in one tab.
2. Negative control: read it back in a second tab of X. If that fails, the probe fails with "negative control
   failed", so a leak cannot hide behind a probe that sees nothing.
3. Read it in Workspace Y. It must not be there.
4. Swap X and Y and repeat ("both directions").

**Expected to fail.** `EXPECTED_TO_FAIL` in `tools/aerie_bench/checks/workspace_isolation.py` lists the probe ids
that are allowed to fail. It holds every probe now. While a probe in the set fails, the check reports `xfail`
(counted as ok, so `./aerie bench --check workspace_isolation` exits 0 and says why). A probe in the set that
passes is an `xpass` and fails the run: delete its id from the set. A probe not in the set that fails fails the run.
M5 is done when the set is empty and the check passes. `--workspaces-enforce` ignores the set. Today's builds have
no Workspaces, so the check reports `xfail` with "chrome://workspace-test is not served".

**The fixture site.** The fixture server serves a test site under `/ws/` (`tools/aerie_bench/fixtures/ws/` and the
routes in `fixtures.py`): a login (a session cookie from `/ws/login` and localStorage), a service worker, a shared
worker, a cacheable and slow resource (`/ws/cached`), HTTP basic auth (`/ws/auth/<tag>/login`, and a probe that never
challenges), a page that visits and a page that styles `:visited`. The site's main origin is
`http://localhost:<port>`. Its second origin is the https twin on a second loopback port: `FixtureServer.enable_tls()`
makes a self-signed certificate with the `openssl` command and the check passes its public key hash to the browser as
`--ignore-certificate-errors-spki-list`, so the browser accepts that one key and no other. It sends the HSTS
header. Paths under `/ws/` keep the connection open, so the Bench can see which socket carried a request; other
paths still close it.

**The driver.** Aerie has no sidebar yet, so a probe cannot switch Workspaces by clicking. It uses a test page
instead, `chrome://workspace-test`, served only with `--aerie-test-workspaces` (the prototype of train 8 has it;
see `tools/aerie_bench/workspace_driver.py` for the JavaScript calls M5's page has to offer). Probes only use the
`Driver` and `Tab` methods, so the unit tests run them against a fake browser with Workspaces
(`tests/workspace_fakes.py`). Each leak the fake can show has a test that proves the right probe fails.

| Probe | What it proves | ADR 0006 row |
| --- | --- | --- |
| `two_logins` | Story 92: user A in Workspace 1 and user B in Workspace 2, one window, each sees only its own cookie and localStorage. | none |
| `cookies` | A cookie set by the server in one Workspace is not sent from the other. | 1 |
| `dom_storage` | localStorage, sessionStorage, IndexedDB, the file system (OPFS), BroadcastChannel, Web Locks, SharedWorker and blob URLs. WebSQL is gone from Chromium. | 2 |
| `cache_storage` | Cache Storage. | 3 |
| `http_cache` | The standard cache probe: a resource cached in X is fast and answers `only-if-cached` in X; in Y it is a miss, slow, and reaches the server again. | 4 |
| `code_cache` | A layout check only: each Workspace partition has its own `Code Cache` directory in the profile. It does not read cache entries. | 4b |
| `service_workers` | A service worker registered in X is not listed in Y. | 5 |
| `hsts` | An HSTS header seen over https in X upgrades http in X and not in Y. | 6 |
| `dns` | A name resolved in X is in X's host cache and not in Y's. The test page resolves through the Workspace's own resolver and reads its host cache, so `--host-resolver-rules` does not hide it. | 7 |
| `sockets` | The NetLog: no socket carried requests of both Workspaces, and sockets were reused inside each one (the control). Judged after the browser exits. | 8 |
| `http_auth` | Basic credentials entered in X are not sent in Y. | 9 |
| `client_cert` | A client certificate choice stored for a server in X is not remembered in Y. The OS certificate store is shared by design and not probed. | 10 |
| `permissions` | A notification grant made in X reads `default` or `prompt` in Y (decision D2, gap G2). | 11 |
| `history` | A page visited in X is absent from Y's history and omnibox suggestions, and a link to it is not styled `:visited` in Y; the Bench reads the link colour from a screenshot (decision D1, gap G8). | 12 |
| `renderer_process` | The two Workspaces have different renderer process ids. | 17 |
| `tab_move` | Story 95: a tab moved to the other Workspace uses that Workspace's login after reload, with no `window.opener` and no sessionStorage. | none |
| `external_link` | Story 94 (decision D4): a link handed over by another app opens a tab in the active Workspace. | 22 |
| `noopener` | `window.open(..., 'noopener')` from a Workspace tab stays in that Workspace (gap G6). | 21 |

A test reads the ADR and fails if a row that says "split" has neither a probe nor an entry in `WAIVED_ROWS` (empty today).

**Known gaps.** No split row of the ADR is left without a probe, so `WAIVED_ROWS` is empty.
The `dns` and `client_cert` probes use the driver calls `resolve`, `dnsCache`, `setClientCertChoice` and
`clientCertChoice`, which the train 8 test page does not have yet.

The `sockets` probe reads the NetLog's source dependencies (URL request, stream job, socket). The parser is tested
on synthetic logs and was checked by hand against one real NetLog from a Bench run (that log is not kept in the repo); it has not seen a log with two
Workspaces. The `history`, `permissions` and `external_link` probes also need calls (`history`, `omnibox`,
`setPermission`, `activateWorkspace`, `openExternal`) that the train 8 test page does not have yet.

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
