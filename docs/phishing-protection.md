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
| Phishing Hosts Blocklist (malware-filter, hosts variant) | malware-filter (curbengh) | `overlay/third_party/filter_lists/phishing_filter_hosts.txt` | Pinned. About 20 700 domains, `0.0.0.0 domain` lines; the engine skips any that are not plain domains. |
| URLhaus | abuse.ch | not pinned | Open question, below. |

The lists are the protection category of the Filter Engine. They sit next to the ad lists and
follow the same rules (see [adblock.md](adblock.md)): committed in the repo, pinned by sha256 in
`protection_lists.lock.json`, never fetched by the build, refreshed by `./aerie lists update`
(one command for both kinds, each list from its publisher's own URL) and a reviewed commit. The build
checks the files in the tree against the lock, and `compile_protection.py` checks them again before it
compiles them.

malware-filter updates its list twice a day (its README; the pinned copy says `# Updated: 2026-11-08T00:02:34Z`
and `# Expires: 12 hours`). Aerie does not update lists at run time (ADR 0001), so the list in a build is as old as
its last reviewed update. The version in the lock is the list's own `# Updated:` time. `./aerie lists update` refuses
a download that has no title and time header and is not nearly all domain-shaped lines, so an error page is never
pinned. It also checks the hosts in `protection_selfcheck.json` against the new list before it pins it, so a host
that dropped out fails at update time and not at build time. The list churns, so the self-check hosts will drop out
now and then. Pick new ones from the new file when that happens.

The hosts variant is used because it holds domains only. The URL-based and wildcard variants also hold IP
addresses, which the engine does not match. The file comes from the project's first mirror,
<https://malware-filter.gitlab.io/malware-filter/phishing-filter-hosts.txt>. The project is also on GitHub
(<https://github.com/curbengh/phishing-filter>) and Codeberg.

**Size and memory.** The list is small. `phishing_filter_hosts.txt` is 744 KB (20 670 domains). The first list Aerie
pinned was 3.1 MB. The list Aerie held before this one, Phishing.Database, was 11 MB. The compiled engine
(`IDR_AERIE_PROTECTION_ENGINE` in `resources.pak`) is about 0.6 MB uncompressed (578 254 bytes, measured with
`aerie_protection_compile` on 2026-11-08), against about 3 MB for the first list and about 10.5 MB for
Phishing.Database. The browser needs an estimated 0.5 MB to 1 MB of memory for the domain set (the 9 MB to 21 MB
estimate for Phishing.Database, scaled to 20 670 entries; not measured in a browser). The engine loads at
start-up, and the first navigations wait for the load. With this size the wait is short, but it has not been
measured in a browser.

**URLhaus.** abuse.ch removed the public host file (`urlhaus.abuse.ch/downloads/hostfile/` answers `410 Gone`,
checked 2026-11-07). Its exports now need a personal Auth-Key in the URL. A key in the repo or in a
build cannot be public, so Aerie pins only the phishing-filter list for now. The parser already reads the host file
format (`0.0.0.0 domain`), so adding URLhaus later is one lock entry once someone decides how the key is kept.

**Licence.** The shipped list is `phishing-filter-hosts.txt` from
[malware-filter's phishing-filter](https://gitlab.com/malware-filter/phishing-filter) (mirrored at
<https://github.com/curbengh/phishing-filter>). The licence statement was read at its source on 2026-11-08
(<https://gitlab.com/malware-filter/phishing-filter#license>). The project licenses its code and its lists
differently:

> src/: Creative Commons Zero v1.0 Universal and MIT License
>
> filters: [CC BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)

The list is under CC BY-SA 4.0. The upstream feeds are credited the same way in the README:
[OpenPhish](https://openphish.com/) is "available free of charge by OpenPhish",
[IPThreat](https://ipthreat.net) is CC BY-SA 4.0, and [PhishTank](https://phishtank.org/) is "available free of
charge by PhishTank". The README adds: "This repository is not endorsed by any of the upstream sources."

CC BY-SA 4.0 requires credit, a link to the licence and a note of changes, and it requires that an adaptation is
shared under the same licence or a compatible one. The GPLv3, which is the licence of Aerie, was declared a
BY-SA-compatible licence for version 4.0 on 8 October 2015. The compatibility is one-way: material under CC BY-SA
4.0 can go into a GPLv3 work, and not the other way round
(<https://creativecommons.org/share-your-work/licensing-considerations/compatible-licenses/>). The compiled
engine is an adaptation of the list. It ships inside the GPLv3 browser, so the share-alike condition holds. The
list itself stays CC BY-SA 4.0 in the repo.

Credit: "malware-filter (curbengh)", and its upstream feeds OpenPhish, IPThreat and PhishTank.
`overlay/third_party/filter_lists/LICENSE.phishing_filter` holds the publisher's statement and the full CC BY-SA 4.0
text, and `README.chromium` in the same folder credits the list and the feeds (both reach `chrome://credits`
through the `filter_lists` dependency). The file in the repo is the file the project served, with no change.

What this check did not cover: the licence is a statement by the project about its own repository.
Aerie did not audit where each domain in the list came from, and the licence is not a legal opinion.

Other candidates, checked 2026-11-08 and not used:

- **Phishing.Database** (MIT) was pinned for a short time. Its list is 11 MB, and the GitHub copy of
  `phishing-domains-ACTIVE.txt` was last committed on 2026-10-02 (`gh api
  repos/Phishing-Database/Phishing.Database/commits?per_page=1`). Its own site `phish.co.za` answered 503.
- **URLhaus (abuse.ch)** has no public feed now (above).
- **OpenPhish's community feed** gave no licence at its feed URL (`openphish.com/phishing_feed.txt` answered
  404 when fetched), so there was no licence text to rely on.

Before a change of list, read the licence at its source again and quote the grant here.

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
  cannot be loaded the browser stops with a `CHECK`, like the ad engine. A list with fewer than 10 000 domains
  (`MIN_ENTRIES` in `rust/core/src/protection.rs`) counts as not loadable, so an empty or truncated list fails
  loudly: the log names the floor and `ListLoadSuccess` is false, which the Bench check reads.
  Without a service (unit tests, shutdown) navigations pass.
- Histograms: `Aerie.Protection.ListLoadSuccess`, `NavigationBlocked`, `UserProceeded`.

Not covered: non-http(s) URLs, IP-address hosts, downloads that start without a navigation, WebSockets,
anything that is not on the pinned list. A domain on the list is stopped even if the page behind it is fine:
lists have false positives. Continue anyway is the answer.

## Build steps

`./aerie build` verifies the committed list (`prepare_adblock`). GN then runs `compile_protection.py` with
`aerie_protection_compile` (Rust, same crates as the browser): it checks the hash, builds the engine,
loads it back and runs the self-checks in `protection_selfcheck.json`: three hosts taken from the pinned
list must be listed, big legitimate sites must not, and the list must hold at least 10 000 domains.
The pinned list has no harmless test domain, so a self-check host can drop out of the list.
`./aerie lists update` checks for that before it pins the list and stops there until a person picks
another host. An empty, truncated or poisoned list stops the build. The engine goes into `resources.pak` as
`IDR_AERIE_PROTECTION_ENGINE`.

## Safe Browsing stays off

Helium compiles Google Safe Browsing out with `safe_browsing_mode=0` in its GN flags. Chromium's default is on, so
`./aerie build` stops before `gn gen`, and `./aerie patches check` fails, if the effective GN args do not
set `safe_browsing_mode` to 0 (`helium.safe_browsing_problems`). A Helium bump that turns it back on is caught
there, so the local list stays the only check.

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
