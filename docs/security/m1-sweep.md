# M1 security sweep

Issue #20. An OWASP Top 10 (2021) pass over Aerie's own code at commit `6216142` (main, 2026-11-08).
The sweep reports and fixes nothing. Each finding is a separate issue labeled `needs-triage`.

## Scope

In scope:

- the Patch Layer (`patches/`), including the adapted Helium patches,
- the overlay (`overlay/`): the Filter Engine (Rust core, cxx bridge, C++ service, request proxy, list
  manager, fetcher, storage, `chrome://filters`, toolbar button; issue #99 later replaced the button with the Guard address-bar icon), cosmetic filtering, phishing protection,
  Widevine opt-in,
- the hardening defaults (`tools/aerie_hardening/controls.json` and the `hardening/` patches), including the
  per-site JIT row,
- the build scripts (`tools/aerie_build/`, `tools/sync-mirrors.sh`, `compile_engine.py`) and the Bench
  launcher.

Out of scope:

- Helium and Chromium code, except where an Aerie patch changes it or depends on it. In those places the
  upstream code was read in the patched tree at `/mnt/linux2/aerie-build/src/helium-0.21.1.1`.
- The extension download proxy (`patches/services/0001`, #19). It has its own sweep, #61.

Method: read the code and the ADRs, map the trust boundaries (web page, renderer, extension, list
publisher, update server, build download hosts), and check each one against the OWASP categories. Two
claims were tested by running code: the match-all exception bypass (a `cargo test` in a scratch copy of the
core crate) and Python's redirect handling. `cargo deny check advisories` on the adblock workspace found no
advisories.

## Findings

| Issue | Title | Category | Severity |
| --- | --- | --- | --- |
| #64 | Stop any exception rule that matches every address from turning off blocking | A04, A08 | Low |
| #65 | Give the CRLSet and PKI metadata components an update path | A02, A06 | Medium |
| #66 | Stop extensions from allowing third-party cookies through chrome.contentSettings | A01, A05 | Low |
| #67 | Refuse non-https redirects when pinning lists with `./aerie lists update` | A08 | Low |
| #70 | Refuse redirects to loopback and private addresses when fetching filter lists | A10 | Low |

## Verdicts by category

### A01 Broken Access Control: finding #66

- **Extensions and hardened settings.** Patch `hardening/0006` removes the `chrome.privacy` write
  permission for three hardened prefs. An extension with `contentSettings` can still allow third-party
  cookies per site, or per top-level domain, through `chrome.contentSettings.cookies`. Upstream treats an
  explicit ALLOW as stronger than the global block (#66).
- **Renderer to browser.** Aerie adds no Mojo interface and no renderer code. Every Aerie GN target is
  visible only to browser-process targets; none is reachable from renderer code. The request proxy, the protection throttle and the cosmetic
  helper run in the browser process. A compromised renderer cannot skip the verdict.
- **WebUI.** `chrome://filters` can only be opened by the user. Web pages cannot load it, and its default
  CSP allows only its own script. `AerieFiltersHandler` checks the type and length of every argument
  (`aerie_filters_handler.cc:24-31`), and the manager checks each value again (list URL, list id, host).
  `AerieHardeningHandler` (patch `hardening/0002`) removes only a canonical host name.
- **Incognito.** An exception set in incognito is kept in memory per profile and cannot undo a saved one.
  Open work on this is #50. The interstitial's "Continue anyway" is per tab, per host, in memory only.

### A02 Cryptographic Failures: finding #65

- **Revocation and CT.** The CRLSet and PKI metadata components never update. Linux gets no CRLSet at all.
  CT enforcement and static pinning stop 70 days after the compiled-in data date (#65).
- **List downloads.** At run time, list downloads are https only, with no cookies, no referrer and no
  cache. A redirect that leaves https stops the download before the request goes out
  (`filter_list_fetcher.cc:122-130`).
- **Secure DNS.** Patch `network/0001` turns secure DNS off by default, so that no DoH provider is
  contacted. This is a decision recorded in the patch and in `docs/network-endpoints.md`. It is a privacy
  trade-off, not a defect, so no finding was filed.
- No secrets, keys or credentials are in the repo. `tools/keys/` holds only the ccache publisher's public
  key. The mirror sync passes its token in the environment, not on the command line.

### A03 Injection: not applicable, no finding

- **Cosmetic injection.** The only script that goes into a page carries the stylesheet as a JSON string
  literal (`base::GetQuotedJSONString`, ASCII only), in Aerie's own isolated world
  (`ISOLATED_WORLD_ID_CONTENT_END`, which no other production code uses). Selectors pass
  `is_safe_selector` when the lists compile and again on output. The class and id names read from the page
  are only lookup keys, size-capped in C++ and in Rust, and never reach a script.
- **Interstitial.** The host is HTML-escaped, and GURL has already made it canonical
  (`aerie_protection_blocking_page.cc:112`).
- **WebUI.** `aerie_filters.js` uses only `textContent` and `createElement`. No `innerHTML` was found in
  the overlay or the patches.
- **Build.** Commands run as argument lists, with no shell. Tar extraction uses `filter="data"` (ccache) or
  checks every member path (crates).
- There is no SQL or other query language in Aerie's code.

### A04 Insecure Design: finding #64

- The ADRs model the trust boundaries well. The engine runs in the browser process, holds requests until it
  is ready, and fails closed. A broken shipped engine or phishing list stops the browser with a `CHECK`.
- One design check is too narrow. The guard against "match-all" exception rules knows five exact
  patterns, so `@@http`, `@@.` and similar lines turn off all network blocking. Such a line passes
  `check_list` too (#64).
- Runtime updates of the built-in lists (ADR 0003) are protected by TLS only, with no signature. That is a
  recorded decision. #64 makes it matter more, because one line from the publisher is enough.

### A05 Security Misconfiguration: finding #66 (see A01)

- **Hardening defaults.** HTTPS-only is strict, third-party cookies are blocked, WebRTC uses the default
  public interface only, preloading is off, site isolation is on, and Widevine is off. Each default has a
  Bench check, and the settings page is generated from the same table, so the three cannot drift apart.
- **The `chrome.contentSettings` gap** is filed as #66.
- **Test switches.** `--aerie-test-allow-loopback-filter-lists` allows only http on 127.0.0.1, ::1 and
  localhost. `--aerie-test-protection-list` can only add blocked domains. The Bench uses
  `--remote-debugging-pipe`, not a TCP port.
- **Widevine.** The component is registered only after the opt-in. Requests to Google's update server name
  only the Widevine app id (`widevine_opt_in.cc:127-133`). The CDM is checked against the pinned key of
  the upstream installer. The prompt is browser UI (an infobar), so a page cannot draw over it. Declines in
  incognito stay in memory until Aerie exits, as documented in `docs/widevine.md:17-18`.
- **JIT toggle.** Aerie adds only a settings link row. The process model is upstream's.

### A06 Vulnerable and Outdated Components: finding #65

- Chromium, Helium, the Helium downloads, ccache, the Python packages and the adblock crates are all
  pinned by commit, by sha256 or by signature. `cargo deny check advisories` on the adblock workspace
  (adblock 0.13.4 and its dependencies) found no advisories. No Python advisory database was available
  here. Pillow is used only by Helium's build tools, and Aerie installs it with `--require-hashes`.
- **Security data components.** The CRLSet and the CT log list are components that go stale and are never
  refreshed (#65).
- Upstream freshness depends on the Helium bump cadence. #17 covers automating the bump.

### A07 Identification and Authentication Failures: not applicable

Aerie's code has no accounts, logins, sessions or passwords. Chromium's password manager and sign-in are
upstream. The extension proxy's access control is #61.

### A08 Software and Data Integrity Failures: findings #64, #67

- **Build downloads.** Every Helium download is checked against `helium.lock` before anything is
  unpacked. A file that fails is renamed `.rejected`. The fallback tarball is checked against both the
  published `.hashes` file and the pin, before and after the download. The ccache archive is checked with
  minisign (`tools/aerie_build/minisign.py`: RFC 8032 verification, the key id must match, the trusted
  comment is signed, and the file name must match). The Python packages are installed with
  `--require-hashes --only-binary=:all:`. Each added crate is checked byte for byte against the locked
  crates.io sha256, and the vendored set must equal the lock. The engine is compiled from lists whose hash
  is checked again in GN (`compile_engine.py`), and the build checks the compiled engine before it uses it.
- **Lists at build time.** The lists are committed and pinned. `./aerie lists update` is the one step that
  trusts a download on first use, and it follows a redirect from https to http (#67).
- **Lists at run time.** User lists and list updates are checked (UTF-8, not HTML, at least one network
  rule, a parse budget) before they replace anything. The match-all guard has a gap (#64).
- **Serialized data.** The serialized engine is loaded only from `resources.pak`, never from disk. The
  protection list's serialized form is checked again line by line when it loads.
- **Mirrors.** `sync-mirrors.sh` never deletes refs or moves tags, and it backs up a rewritten branch in the
  same atomic push.

### A09 Security Logging and Monitoring Failures: no finding

A browser that sends no telemetry has no central monitoring. Local signals exist: UMA histograms for
engine load, list update success, protection blocks and interstitial choices (local only, nothing is
reported), and `LOG(ERROR)` when a list fails. No log line holds a URL, a host or a user value.
The one silent case, a list that turns off all blocking and still shows as "updated", is covered by #64.

### A10 Server-Side Request Forgery: finding #70

Aerie runs no server. The one fetch whose address the user chooses is a custom filter list. It is https
only, and it goes through the system network context with no credentials. The address the user types is
one they could open in a tab. A redirect is different: the list publisher chooses it, not the user.
`FilterListFetcher::OnRedirect` (`filter_list_fetcher.cc:122`) only calls `IsAcceptableListUrl`, which
accepts any https host, including loopback, private-network and link-local IP literals. A list server can
send the daily background fetch to an internal https address, and Private Network Access does not check
it. The impact is a blind GET with no credentials and no response to the publisher (#70).

## Browser-specific checks

### Renderer to browser trust boundaries

- **Request proxy.** `ClassifyRequest` reads `request.trusted_params->isolation_info` before the network
  service has checked the request (`request_classifier.cc:78-90`, called at line 107). A compromised renderer could set
  `trusted_params` to make an ad look first party to an excepted site. The network service refuses
  `trusted_params` from an untrusted factory, so that request fails and the renderer is killed. Nothing
  gets past the blocker this way. `request_initiator` is used only when no top-frame origin is known, and
  the network service checks it against the initiator lock.
- **Cosmetic tab helper.** Page data reaches the browser only as the result of Aerie's own collect
  script, in its own world. `ParsePageTokens` caps the size, and Rust caps the count and length again. A
  page can make the browser hide fewer elements, never run script.
- **Widevine prompt.** It is triggered by `KeySystemSupportImpl::SetObserver` (patch `widevine/0004`). A
  page can only make the infobar appear, one at a time per tab. It cannot turn Widevine on.
- **Interstitial.** `CommandReceived` accepts only proceed and go back (`aerie_protection_blocking_page.cc`).
- **No finding.**

### Parsing untrusted data

- **Filter lists** (`lib.rs`, `lists.rs`): lossy UTF-8, line and size caps, only plain cosmetic rules are
  kept, and the code never panics (tested with garbage, huge lines and regex bombs). The `regex` crate runs
  in linear time. **Finding #64.**
- **Domain lists** (`safety.rs`): strict host grammar, at least two labels, no IP addresses, entry and size
  caps. The serialized form is checked again on load.
- **`state.json`:** a size cap, then RFC JSON. Ids must match `[a-z0-9]{1,32}`, a custom id must be the
  hash of its URL (so a file name cannot be chosen), only known error texts are kept, and titles and
  counts are clamped.
- **List downloads:** a 5 MiB cap, a 60 s timeout, and a parse budget off the UI thread.

### Supply-chain integrity of sources and lists

| Input | Pin | On failure |
| --- | --- | --- |
| Helium source tarballs and deps | sha256 in `helium.lock` | the file is renamed `.rejected` and the build stops before unpacking |
| Official Chromium fallback | published `.hashes` and the pin must agree | nothing is downloaded or unpacked |
| ccache | minisign, publisher key in `tools/keys/` | the file is renamed `.rejected` and the build stops |
| Python packages | `--require-hashes`, wheels only | pip fails |
| adblock crates | crates.io sha256 in `rust/crates.lock.toml`, vendored files compared byte for byte | the build stops |
| EasyList, EasyPrivacy, phishing-filter (build) | sha256, size, version header where the list has one, checked twice | the build stops |
| `./aerie lists update` | trust on first use, then review | https to http redirect followed (**#67**) |
| List updates at run time | TLS only, then `check_list` | the old copy is kept (**#64** for the guard gap) |
| Widevine CDM | upstream installer's pinned key hash | not loaded |
| CRLSet, PKI metadata | none, never updated | **#65** |

## Not filed, and why

- **Secure DNS off by default** (`network/0001`): a deliberate privacy trade-off, documented.
- **Runtime list updates without a signature** (ADR 0003): a deliberate decision. #64 covers the most
  damaging consequence.
- **Incognito Widevine declines kept until exit:** documented behavior.
- **The settings handler `CHECK`s the argument count** (patch `hardening/0002`): the usual Chromium
  pattern for WebUI handlers. Only the settings page can send these messages.
- **Downloads, WebSockets and main frames are not filtered by the ad blocker:** documented scope (ADR 0001).
  Phishing protection covers main frames and downloads, through the navigation throttle.
- **Issues already open that touch security:** #40 (MV2 webRequest), #44 (cosmetic hardening), #50
  (incognito exceptions), #52 (phishing tidy-up), #56 (Widevine follow-ups), #61 (proxy sweep). No
  duplicate was filed.
- **Follow-ups:** #70 (redirect to a private address in the list fetcher, filed by this sweep), and one
  issue for the onboarding page: Helium's `OnboardingMessageHandler` lets the page set any `helium.*` pref
  and still registers `installExtension` (found while reviewing PR #63) (#69).
