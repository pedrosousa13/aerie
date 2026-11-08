# Phishing and malware protection

Aerie checks every web address on your computer against lists of phishing and
malware domains. It does not use Google Safe Browsing. It sends nothing about
the pages you visit to anyone. Decisions and reasons:
[ADR 0004](adr/0004-local-phishing-and-malware-protection.md).

## Status

Written, with the Rust code, the Python tooling and the Bench check tested. The C++ (service, navigation
throttle, interstitial) passes a syntax check against the real Chromium headers
(`tools/check-overlay-syntax.sh`) but is not built or run yet: `./aerie bench --check phishing_protection` on the
combined build is the proof.

## The lists

| List | Publisher | Where it is | Status |
| --- | --- | --- | --- |
| Phishing Army (extended) | phishing.army | `overlay/third_party/filter_lists/phishing_army.txt` | Pinned. About 145 000 domains. |
| URLhaus | abuse.ch | not pinned | Open question, below. |

The lists are the protection category of the Filter Engine. They sit next to the ad lists and
follow the same rules (see [adblock.md](adblock.md)): committed in the repo, pinned by sha256 in
`protection_lists.lock.json`, never fetched by the build, refreshed by `./aerie lists update`
(one command for both kinds, each list from its publisher's own URL) and a reviewed commit. The build
checks the files in the tree against the lock, and `compile_protection.py` checks them again before it
compiles them.

Phishing Army expires its list after four hours. Aerie does not update lists at run time (ADR 0001), so
the list in a build is as old as its last reviewed update.

**URLhaus.** abuse.ch removed the public host file (`urlhaus.abuse.ch/downloads/hostfile/` answers `410 Gone`,
checked 2026-11-07). Its exports now need a personal Auth-Key in the URL. A key in the repo or in a
build cannot be public, so Aerie pins only Phishing Army for now. The parser already reads the host file
format (`0.0.0.0 domain`), so adding URLhaus later is one lock entry once someone decides how the key is kept.

**Licence.** Phishing Army adds no restriction on use of its feeds, but it does not give rights to the
upstream data, whose terms can differ for products (see `LICENSE.phishing_army`; it is credited in
`README.chromium`). Check this before a release.

## How a navigation is judged

```
navigation start / redirect hop ──> AerieProtectionThrottle (browser process, defers)
                                        │ host
                                        ▼
                               ProtectionService ──posts──> its own sequence ──> ProtectionEngine ──cxx──> domain set
```

- One hook patch, `patches/adblock/0011`, adds the throttle first in
  `ChromeContentBrowserClient::CreateThrottlesForNavigation`. The throttle runs in the browser process,
  before the request goes out, so the blocked site never runs code and cannot style or script the page.
- The throttle judges the start and every redirect hop, of the main frame and of every subframe, with the same
  function (`Decide()` in `components/aerie_adblock/browser/protection_policy.cc`). A listed domain covers
  all its subdomains, on label boundaries.
- Main frame: the interstitial. It is Chromium's shared interstitial page in its red safe browsing look,
  with Aerie's own text. **Go back** is the primary button. **Continue anyway** is a link inside the
  details section, which is closed at first. Subframe: the frame fails with `ERR_BLOCKED_BY_CLIENT`
  (no buttons). Prerender: cancelled.
- **Continue anyway** is kept for one tab, for that exact host, in memory
  (`AerieProtectionTabState`). It ends with the tab, so it never outlives the session, and it does not reach another
  tab, profile or incognito window.
- The service answers after the list has loaded, so a navigation that starts early waits. If the shipped list
  cannot be loaded the browser stops with a `CHECK`, like the ad engine. Without a service (unit tests,
  shutdown) navigations pass.
- Histograms: `Aerie.Protection.ListLoadSuccess`, `NavigationBlocked`, `UserProceeded`.

Not covered: non-http(s) URLs, IP-address hosts, downloads that start without a navigation, WebSockets,
anything that is not on the pinned list. A domain on the list is stopped even if the page behind it is fine:
lists have false positives. Continue anyway is the answer.

## Build steps

`./aerie build` verifies the committed list (`prepare_adblock`). GN then runs `compile_protection.py` with
`aerie_protection_compile` (Rust, same crates as the browser): it checks the hash, builds the engine,
loads it back and runs the self-checks in `protection_selfcheck.json` (the publisher's test domain
`amiprotected.it` must be listed, big legitimate sites must not, and the list must hold at least 50 000
domains). An empty, truncated or poisoned list stops the build. The engine goes into `resources.pak` as
`IDR_AERIE_PROTECTION_ENGINE`.

## The Bench check

`./aerie bench --check phishing_protection`. The Bench cannot use the real list, so it starts a second
browser with `--aerie-test-protection-list=<file>`, which adds fixture domains (`*.localhost`) to the list.
The switch only adds blocked domains. The first browser, without the list, is the negative control.
The check proves: the interstitial (not the site's script or style, no request at the server), Go back, Continue
anyway for one tab only, a blocked subframe, and redirect chains in the main frame and in a subframe. It also
needs the browser's own histograms, so another feature cannot take the credit. See [bench.md](bench.md).

## Tests

| What | Command |
| --- | --- |
| Python (list tooling, build action, Bench judge, fixture hop) | `python3 -m unittest discover -s tests -t . -v` |
| Rust (domain set, bridge, build tool) | `cd overlay/components/aerie_adblock/rust && cargo test` |
| The real tool on the real list | `AERIE_TEST_CARGO=1 python3 -m unittest tests.test_compile_protection -v` |
| C++ syntax against a built tree (read-only) | `tools/check-overlay-syntax.sh` |
| C++ unit tests (in the train) | `autoninja -C out/Default aerie_adblock_unittests` |
| Blocking, for real (in the train) | `./aerie bench --check phishing_protection` |

## Helium dependency

None. The hook patch edits lines that Chromium owns, and nothing here calls code or settings that a Helium
patch adds. See [helium-dependency-ledger.md](helium-dependency-ledger.md).
