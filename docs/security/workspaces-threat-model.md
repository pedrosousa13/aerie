# Workspaces threat model

Issue #72. Status: accepted with [ADR 0006](../adr/0006-workspace-isolation.md) (accepted, 2026-11-08; decisions D1 to D4 are applied below). It covers the
recommended design: one profile, one `StoragePartition` per Workspace, tabs fixed to a Workspace when created.
Nothing here was run. Mitigations marked "to verify" are Bench or build-train checks (#73).

## Assets

| Asset | Where it lives | Why it matters |
| --- | --- | --- |
| A1. Login state: cookies, tokens in localStorage or IndexedDB, service worker state | Partition (per Workspace) | Story 92: account A must not act as account B. |
| A2. Cached content of logged-in pages | Partition HTTP cache, Cache Storage | Private pages (mail, banking) readable by timing or by a cache hit. |
| A3. Network session state: HSTS, HTTP auth, client-certificate use, open connections | Partition `NetworkContext` | Carries a credential or a pinned policy across accounts. |
| A4. Consent: permission grants (camera, location, notifications) | Profile (shared today, G2) | A grant for one account becomes a grant for the other. |
| A5. Saved credentials, autofill, passkeys | Profile (shared, decision) | Account B's password offered to a page of A. |
| A6. History and visited-link state | A `HistoryService` per Workspace (D1, gap G8) | Reveals what the other Workspace visited. |
| A7. The fact that one person owns both accounts (linkability) | Fingerprint, shared profile state | A site that sees both Workspaces can join the identities. |
| A8. The Workspace list (names, ids, which tab is where) | Profile pref and tab model | A name like "Clinic" can be sensitive. |

## Trust boundary

The boundary is **the `StoragePartition` and the renderer process**: web content in Workspace A can reach only its
own partition's storage and network state, and never shares a renderer process with Workspace B. It is enforced by
Chromium's process and storage design: a `RenderProcessHost` has one `StoragePartition`, the partition is part of
`SiteInfo`, and each partition has its own `NetworkContext`.

It is **not** a boundary against:

- Browser-process code with profile access: history, passwords, permissions, extensions, the Filter Engine state. These
  are trusted, and are shared on purpose (ADR 0006 verdicts 11 to 15).
- The user's own mistakes: moving a tab to the wrong Workspace, or opening a link in the wrong one.
- A compromised browser process or a stolen unlocked device.
- Device fingerprinting by a site that sees both Workspaces (A7). The Workspace is a login boundary, not an anonymity
  boundary.

## Attacker models

### T1. A malicious site in Workspace A

Goal: read or use Workspace B's data, learn that B exists, or reach the default partition.

| Attack | Mitigation | Status |
| --- | --- | --- |
| Read B's cookies, storage, caches, service workers | Separate partition per Workspace, separate renderer process, origin lock. | By design. Bench: both directions (#73). |
| Cache probe: time a fetch to learn whether B loaded a resource | A separate HTTP cache per partition. | By design, to verify: cache-probe check. |
| HSTS probe: load `http://x` and see if it upgrades | HSTS is per `NetworkContext`. | By design, to verify. |
| Reuse B's connection, client certificate or HTTP auth | Separate `HttpNetworkSession`. | By design, to verify: NetLog socket-reuse check. |
| `window.open(..., 'noopener')` or `target=_blank rel=noopener` to escape to the default partition, where another account may be logged in | `CreateNewWindow` keeps the opener's fixed partition. | Upstream hole G6. Prototype patch 0002. Bench check needed. |
| Make the browser fetch for it in the default partition (preconnect, prefetch, prerender, Early Hints) | Aerie turns preloading off by default; the loading predictor uses the default partition. | Closed by default (G5). Bench: keep checking. |
| Get a permission B granted | Grants are per profile and origin, so site `x.test` in A has B's grant for `x.test`. | **Open: G2, required (D2).** Mitigation: Workspace-keyed permission layer. Train 8 confirmed the leak with a notification grant. |
| Learn what B visited, with `:visited` | History is split per Workspace (D1): each Workspace has its own `HistoryService`, and its visited-link table goes only to renderers of that Workspace. | **Open: G8.** Bench check: a link visited in B is not `:visited` in A. |
| Join A and B by fingerprint, device id salt | Not mitigated. | Accepted, R-5. |
| Read clear-data leftovers after the user cleared data | "Clear browsing data" must visit every Workspace partition. | **Open: G3.** |

### T2. A tab moved between Workspaces

Goal: carry state from A into B, or make the user act in the wrong account.

| Attack or failure | Mitigation | Status |
| --- | --- | --- |
| The page keeps A's in-memory state (JS heap, sessionStorage, opener, bfcache, blob URLs) after the move | A move creates a new `WebContents` in the target partition and closes the old one. Only the URL is carried: no referrer, no opener, no form data, no POST body. | Prototype `MoveTabToWorkspace`. To verify: after a move, neither storage nor `window.opener` survives. |
| A pending permission prompt, download or autofill popup follows the tab | The old tab is closed, and its prompts end with it; downloads belong to the profile and are not tied to the Workspace. | To verify. |
| The page loads in B's logins (story 95 expects this) and the user does not notice | The tab shows its Workspace (icon and colour, per Brave's tooltip and hover card). The move command says so. | M5 UI. |
| After a restart the tab reopens in the default partition | The tab model stores `workspace_id`; a restore with an unknown Workspace opens blank. | **Open: G4.** |
| Session history navigation entries from A reused in B | The new tab has a fresh history. The old entries die with the old tab. | By design. |

### T3. A link opened from another app

Goal: use a link to make the user act in the wrong account (login CSRF, a confused deputy), or to open a page in a
partition the sender chose.

| Attack | Mitigation | Status |
| --- | --- | --- |
| An external link has no source tab, so it lands in the default partition or in the wrong Workspace | Decision D4: open it in a **new** tab in the active Workspace of the last active window, never navigating an existing tab, and the tab shows its Workspace. | M5, story 94. Prototype only adds the carrier (`NavigateParams::storage_partition_config`). |
| The link performs an action as whichever account is logged in there | The new tab shows its Workspace before the page can be trusted. No auto-submit changes. An optional "ask which Workspace" mode for external links. | M5 UI. |
| A link or protocol handler picks a Workspace by name or id (`aerie://open?workspace=...`) | No such URL exists. Workspace ids are not addressable from outside the browser. A command-line `--aerie-test-*` switch exists for the Bench only, as for other test hooks, and is documented there. | By design. |
| A web page asks the browser to open a Workspace-bound tab through a custom scheme | Web pages cannot name a Workspace. A tab opened by a page inherits that page's Workspace. | By design. |

### T4. An extension

Goal: read or change data of a Workspace the user did not mean the extension to see.

| Attack | Mitigation | Status |
| --- | --- | --- |
| A content script runs in a Workspace B page and reads it, or sends it away | Extensions are one set per profile and are trusted with all browsing. Decision D3 (mitigation M-D1): a per-extension "allow in all Workspaces" setting, off for new installs, enforced where Chromium decides page access for a tab (to be placed on the train). | **Open: M-D1 to build (size M).** |
| `webRequest` sees `Cookie` and `Authorization` headers of B | Same. MV2 stays supported in Aerie (see `docs/adblock.md`), so a blocking `webRequest` extension sees them. | Accepted for extensions the user allowed (M-D1), R-1. |
| `chrome.cookies` reads B's cookies | It reads only the default partition (`extensions/browser/api/cookies/cookies_api.cc:120`, `ParseStoreCookieManager`), so it cannot. | By design; a side effect is that cookie-manager extensions do not work in Workspaces. |
| An extension opens a tab in a chosen Workspace | `chrome.tabs.create` goes through the navigator: no partition is named, so the tab inherits the active Workspace or the default. There is no extension API to name one. | By design. |
| Extension storage links A and B | Extension storage is per profile. | Accepted, R-1. |

## Residual risks

| Id | Risk | Why it is accepted | Review trigger |
| --- | --- | --- | --- |
| R-1 | Extensions see all Workspaces | An extension is trusted with all browsing. M-D1 narrows it to extensions the user allowed in Workspaces (D3). | A change to D3 |
| R-2 | A moved tab loses in-page state | The only way to keep isolation. | Never; it is the design. |
| R-4 | The password manager offers all accounts | It is the wanted behaviour. A page needs a gesture to receive a password. | None |
| R-5 | Fingerprint and device ids link Workspaces | One device. Cannot be split without anti-fingerprinting, which is outside this issue. | An anonymity feature request |
| R-6 | GPU shader cache, OS DNS cache, font cache are shared | No account data; timing signals only. | A cross-Workspace timing result in the Bench |
| R-7 | A link from another app acts as the active Workspace's account | The rule is decided (D4); the Workspace is shown on the tab. | A change to D4 |

## Open items for the build train

G1 to G8 and the verifications in the tables are the checklist. They map to #73's `workspace_isolation` suite as
listed in [prototypes/workspaces/REPORT.md](../../prototypes/workspaces/REPORT.md).
