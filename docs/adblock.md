# Native ad blocking (the Filter Engine)

Aerie blocks ads and trackers in the browser itself. The engine is Brave's
adblock-rust (MPL-2.0, used unmodified). The lists are EasyList and EasyPrivacy.
Aerie wrote its own bridge, service and request proxy; it did not copy
brave-core. Decisions and reasons: [ADR 0001](adr/0001-native-adblock-engine.md).

## Status

The engine, the proxy (including redirect hops) and the build steps are written, and Helium's bundled
uBlock Origin is removed: `patches/helium-drop.txt` drops its six patches, `patches/helium-adapt.txt` replaces
two Helium patches whose hunks sat next to it, and the build does not download its assets. Manifest V2 support is
a different patch (`ungoogled-chromium/extensions-manifestv2.patch`) and stays, so users can install uBlock Origin
themselves. On a real build, `./aerie bench --check ad_blocking --check mv2_extension` is the proof. The Patch Layer
feature and every dropped patch are explained in [helium-dependency-ledger.md](helium-dependency-ledger.md).

The two adapted Helium patches are pinned by sha256 to Helium's originals, so a Helium bump that touches them
fails `./aerie patches check` until they are re-cut. The method is in CONTRIBUTING.md ("Re-cutting an adapted
patch"). The onboarding page's uBlock Origin toggle is removed by `patches/adblock/0006` (the asset is not part of
Helium's patches).

## How a request is judged

```
renderer ──Mojo──> AdblockProxyingURLLoaderFactory ──> network service
                          │  (browser process, UI thread)
                          ▼
                   FilterEngineService ──posts──> its own sequence ──> AdblockEngine ──cxx──> adblock-rust
```

- `ChromeContentBrowserClient::WillCreateURLLoaderFactory` calls
  `aerie_adblock::MaybeProxyURLLoaderFactory` (one hook patch,
  `patches/adblock/0001-hook-url-loader-factory.patch`). It puts the proxy in the
  factory chain for document sub-resources, subframe navigations, workers, service
  workers, prefetch and early hints. Downloads and DevTools requests are not covered.
- The proxy runs in the browser process. The renderer holds the other end of a
  Mojo pipe. A compromised renderer cannot skip, read or change the verdict.
  The GN `visibility` of every Aerie target lists only `//chrome/browser:core`,
  so no renderer target can depend on them.
- `ClassifyRequest` (pure code, unit tested) turns a request into: URL, host,
  first party host, request type, third party or not. The first party is the
  top-level site: the request's trusted `IsolationInfo`, else the factory's (the
  hook passes it), else the initiator. So an ad iframe's own pixels are third
  party to the page, and a reload of an ad subframe knows its page. Third party
  uses Chromium's public suffix list (`SameDomainOrHost`), so adblock-rust's own
  domain resolver is never needed. It skips: non-http(s) URLs, main-frame
  documents, and first parties that are browser pages or extensions. Only
  beacons and `<a ping>` are pings; embed and object are `object`.
- `FilterEngineService` owns the one engine of the process. It is owned by a
  `ChromeBrowserMainExtraParts` (`chrome/browser/aerie_adblock`, hook patch
  `0004`): created before any profile, destroyed at shutdown. There is no
  singleton. Factories hold a `WeakPtr` and abort requests once it is gone. The
  engine lives on a thread pool sequence. The load and every query are posted to that
  sequence, so a query that comes before the load ends runs after it: requests
  are held until the engine is ready, never let through unchecked.
- **Redirects.** A judged request that is allowed is wrapped in an
  `AdblockProxyingURLLoader` (one per request). It forwards every
  `URLLoaderClient` call and `SetPriority`, and when the client answers a
  redirect with `FollowRedirect()` it judges the target (the client's URL if it
  names one) with the same top-level-site context as the first URL. The request
  for the new URL only starts when `FollowRedirect()` reaches the network
  service, so a blocked hop never goes out: the client gets
  `ERR_BLOCKED_BY_CLIENT` and the network request is cancelled. Requests that are
  never judged (main frames, non-http, browser pages) skip the wrapper. This
  Chromium's `URLLoader` interface has only `FollowRedirect` and `SetPriority`
  (no pause or resume calls).
- A blocked request fails with `net::ERR_BLOCKED_BY_CLIENT`. Nothing else about a
  request changes.
- If the shipped engine cannot be loaded, the browser crashes with a `CHECK`
  (after recording `Aerie.Adblock.EngineLoadSuccess=false`). The engine ships
  inside the build, so a failure means the build is broken, and failing open is
  not allowed. The build makes this a "cannot happen" case (see below).
- Histograms (declared in `overlay/tools/metrics/histograms/metadata/aerie/`):
  `Aerie.Adblock.EngineLoadSuccess`, `EngineLoadTime`, `RequestBlocked`. The Bench
  reads them.

Not in this slice: WebSocket blocking, main-frame blocking (both out of scope in
the issue), user controls (issue #10).

## Cosmetic filtering (hiding leftover ad containers)

Blocking a request leaves an empty box where the ad would have been. The Filter Engine
also reads EasyList's plain hide rules (`##.ad-slot`, `site.com##.x`, `#@#` exceptions)
and hides those elements.

```
tab ──> AerieCosmeticTabHelper ──> FilterEngineService ──posts──> engine (cosmetic_stylesheet, generic_stylesheet)
  │        (browser process)                                              │
  └── isolated world: adoptedStyleSheets <──── CSS: one `sel{display:none!important}` line per selector
```

- `chrome/browser/aerie_adblock/aerie_cosmetic_tab_helper.*` is a `WebContentsObserver`, one
  per tab (hook patch `0007`, two `CreateForWebContents` calls in `TabFeatures`). When a
  frame commits it asks the service for the stylesheet for that URL (rules for the site, and
  the generic rules that do not start with a class or id) and injects it. At
  `DOMContentLoaded` and again at load it reads the frame's class names and ids, asks for the
  generic rules that match, and injects those.
- Injection runs `ExecuteJavaScriptInIsolatedWorld` and adds a constructed stylesheet
  (`document.adoptedStyleSheets`). The page's Content-Security-Policy `style-src` does not
  govern it and no DOM node is added. The page's scripts do not share JavaScript objects
  with the isolated world, so the Bench fixture tries to hijack `CSSStyleSheet` and the
  `adoptedStyleSheets` accessor and must see nothing.
- Nothing in the renderer was added. No Mojo interface, no renderer target.
- The hook for issue #10: `FilterEngineService::SetCosmeticFilteringPolicy(callback)`. The
  callback gets the top-level page URL and says whether cosmetic filtering is on. The default
  is on. A frame answers to its top-level page, so an ad iframe is not judged by its own
  host. A policy change reaches a page on its next navigation (stylesheets already
  injected stay).
- Output size is bounded: `Aerie.Adblock.CosmeticRuleCount` records the rules of each
  stylesheet; the core crate stops at 20 000 rules and 1 MiB, and reads at most 5 000 classes
  and 5 000 ids from a page.

### Security: what a list or a page can make the engine do

- Only `##` and `#@#` rules survive `sanitize_list`. Scriptlets (`##+js(`), HTML filters
  (`##^`), procedural rules (`#?#`), style injection (`#$#`) and `:style(` / `:remove(`
  actions are dropped when the lists are compiled, so the engine has no script to hand out.
- A selector is kept only if `is_safe_selector` accepts it: no `{ } ;`, no comments, no
  backslash escapes, no `<`, no `@`, no `url(`, balanced brackets, parentheses and
  quotes, at most 2 KiB. This runs when the lists are compiled and again on every stylesheet
  that leaves the engine, because adblock-rust is built without its `css-validation` feature
  (no extra crates to vet).
- One rule per selector: a selector a browser cannot parse voids only itself.
- The stylesheet goes into a script as a JSON string literal (`base::GetQuotedJSONString`).
  The only page-controlled text the browser reads is the class and id list, as engine lookup
  keys; it never reaches a script. The script that reads them only reads the DOM.
- The engine is the build's, from pinned lists: a page cannot add a rule.

### Limits (first slice)

- Elements whose class or id first appears after the load event are not hidden (no mutation
  observer yet).
- `site.*##rule` entity rules use a heuristic registrable domain (`AerieDomainResolver` in the
  core crate), because the embedded public suffix list is not compiled in and adblock-rust
  panics in cosmetic matching without a resolver. An unusual public suffix can miss an
  entity rule; it cannot apply a hide to a site it should not.
- Procedural cosmetic rules (`:has-text`, `:xpath`) and scriptlets are not supported.
- The engine grew from 4.7 MB to 6.0 MB with the cosmetic rules.

## What is built

`overlay/components/aerie_adblock/`:

| Path | What |
| --- | --- |
| `rust/core` | `aerie_adblock_core`: engine over adblock-rust. No C++, no panics, size and line limits. |
| `rust/ffi` | `aerie_adblock_ffi`: the cxx bridge. Four functions. No `Result` (Chromium has no C++ exceptions). |
| `rust/tool` | `aerie_adblock_compile`: host tool, lists in, serialized engine out, with self-checks. |
| `browser/` | C++: classifier, engine wrapper, service, proxying factory, hook, cosmetic scripts, unit tests. |
| `selfcheck.json`, `compile_engine.py` | Build-time checks of the compiled engine against the real lists. |
| `resources/*.grd` | Packs the engine into `resources.pak` (hook patch `0002`). |

`overlay/third_party/filter_lists/`: the list lock, `README.chromium`,
GPL-3.0 text. The lists themselves are downloaded at build time.

The engine is compiled at build time (about 0.2 s, 4.7 MB for both lists) and
loaded at startup, so the delay before the first request is short.

## The build, step by step

`./aerie build` runs these after Helium's steps. Each step stops the build on
any problem.

1. **Overlay.** `overlay/` is an Aerie layer (`OverlayLayer` in
   `tools/aerie_build/overlay.py`). It runs after the patch series and before the
   Helium to Aerie substitution, on every build, with the same pristine and
   restore rules as the other layers (CONTRIBUTING.md, "Changing the Aerie layers
   of an existing tree"). It only adds files; a file that exists upstream is an
   error.
2. **Filter lists.** EasyList and EasyPrivacy are committed in the repo
   (`overlay/third_party/filter_lists/`, with the lock and the credits files) and
   arrive with the overlay. After the layers, `prepare_adblock` in
   `tools/aerie_build/pipeline.py` checks the copies in the tree against
   `filter_lists.lock.json` (sha256, size, the list's own `! Version:` line) and
   fetches nothing. Later, GN runs `compile_engine.py`, which checks the hashes
   again, compiles the lists, loads the result back and runs the self-checks in
   `selfcheck.json` (known ad hosts blocked, first-party allowed). An empty,
   truncated or wrongly parsed list fails there.
3. **Crates.** `prepare_adblock` runs `gnrt vendor` and `gnrt gen` under the crate lock
   `rust/crates.lock.toml`. See below.

### Refreshing the lists

easylist.to stays the recorded source, but it always serves the newest list, so
the lists change only through a reviewed commit:

```
./aerie lists update      # downloads both, checks title and version, writes the files and the lock
git diff --stat overlay/third_party/filter_lists/
```

Review it, update the versions in `README.chromium`, commit. The lists expire after
4 days upstream. Aerie does not update lists at run time.

### The crates

Chromium keeps third-party crates in `third_party/rust`, made by `gnrt` from
`chromium_crates_io/Cargo.toml` and `Cargo.lock`. Patch `0003` declares one
dependency and fixes the gnrt settings it needs, `adblock = "=0.13.4"` (no default features; `full-regex-handling`,
`single-thread`). That brings in 12 new crates (arrayvec, flatbuffers, idna,
idna_adapter, percent-encoding, rustc-hash, rustc_version, seahash, semver,
thiserror 1.x and its macro) and changes 5 lock entries.

`rust/crates.lock.toml` records those packages, the crates.io sha256 of each added crate, and
the hashes of the `Cargo.toml` and `Cargo.lock` it was resolved against. The
build:

1. stops if Chromium's two files differ from those hashes (stale lock);
2. writes the locked entries into `Cargo.lock`, so `gnrt vendor` cannot choose other versions;
3. checks the gnrt settings Aerie's patch sets (adblock and regex in group `safe`, flatbuffers without its `build.rs`, thiserror v1 and v2 apart, aho-corasick allowed `unsafe`) and runs `gnrt vendor` and `gnrt gen`;
4. checks that the final `Cargo.lock` is the original plus the locked entries and nothing else;
5. downloads each added `.crate` from crates.io, checks its sha256, and checks that
   every file in it is in `chromium_crates_io/vendor` byte for byte;
6. checks that `third_party/rust/adblock/v0_13/README.chromium` says
   `License: MPL-2.0` and `Shipped: yes`, with a licence file that exists. That is
   what puts adblock-rust on `chrome://credits`.

Two added crates publish no licence text: flatbuffers (Apache-2.0) and seahash
(MIT). gnrt will not write a `README.chromium` without one, and Chromium credits
every shipped crate. The overlay adds Chromium-style crate patches,
`third_party/rust/chromium_crates_io/patches/{flatbuffers-v25,seahash-v4}/`, that
add each project's upstream `LICENSE` file. `gnrt vendor` applies them when it
downloads the crate. The flatbuffers text is the repository's `LICENSE` at the
commit the crate was published from (the same bytes as Chromium's
`//third_party/flatbuffers/src/LICENSE`). The seahash text was added upstream after
4.1.0 (redox-os/seahash `3088c5c`). gnrt skips a crate that is already vendored, so
the vendor step removes an added crate that has crate patches before it runs gnrt,
and the crate patches are part of the step's stamp. Drop a patch when a release of
its crate ships the file.

Chromium removes the `checksum` lines from its lock and empties the checksum
files of vendored crates, so cargo cannot do step 5 for us.

After a Chromium bump the lock is stale on purpose. Run `./aerie crates resolve`
(reads the tree, needs crates.io, writes only `rust/crates.lock.toml`), review
the diff, commit it.

## Run the tests

| What | Command |
| --- | --- |
| Python (build steps, Bench check logic) | `python3 -m unittest discover -s tests -t . -v` |
| Rust, FFI from C++, build tool (needs cargo, a C++ compiler, crates.io) | `AERIE_TEST_CARGO=1 python3 -m unittest tests.test_adblock_rust -v` |
| Rust only | `cd overlay/components/aerie_adblock/rust && cargo test` |
| Patches apply | `./aerie patches check` |
| C++ syntax against the real headers of a built tree (read-only, needs cargo) | `tools/check-overlay-syntax.sh` |
| C++ unit tests (in the train) | `autoninja -C out/Default aerie_adblock_unittests` |
| Blocking, for real (in the train) | `./aerie bench --check ad_blocking` |
| Hiding, for real (in the train) | `./aerie bench --check cosmetic_filtering` |

## Credits

- adblock-rust: MPL-2.0, through the gnrt `README.chromium` (step 6 above).
- EasyList, EasyPrivacy: dual GPL-3.0-or-later / CC BY-SA 3.0, used under the GPL
  option. `overlay/third_party/filter_lists/README.chromium` and `LICENSE`,
  reached by the `filter_lists` GN group so that `licenses.py` finds them.
