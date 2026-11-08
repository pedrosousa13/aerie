# 3. Let users manage filter lists and turn blocking off per site

Status: accepted (issue #10). Changes two decisions of [ADR 0001](0001-native-adblock-engine.md): "no user controls" and "the build never fetches, lists change only through a reviewed commit" now apply to the shipped lists only.

## Context

The Filter Engine of #8 blocks with two lists that ship in the build. Users need to add their own lists, keep lists fresh, turn blocking off on a site that breaks, and see what was blocked.

## Decision

- **State is a file of Aerie's own**, `<user data dir>/AerieFilterLists/state.json` plus one file per list, not Chromium prefs. The engine is one per process, so the settings are one per process, and no pref registration patch is needed. Writes are atomic.
- **Built-in lists stay and cannot be removed.** Their text goes into `resources.pak` so the engine can be compiled from text after any change (serialized engines cannot be merged). Until something changes, the shipped serialized engine is used, so a user who never opens the settings pays nothing.
- **Downloads are https only**, with no cookies, referrer or cache, a 5 MiB cap (the bounded `SimpleURLLoader` limit) and a 60 s timeout. A redirect that leaves https stops the download, and the loader is deleted inside the redirect callback, so no request goes to the non-https address. Loopback http is allowed only with a test switch, as for the extension proxy, because the Bench server has no TLS.
- **A list is checked before it replaces anything**: UTF-8 text, not HTML, at least one network rule, a parse budget. adblock-rust accepts any text as rules, so "it parsed" proves nothing. A failed list keeps the old copy and shows an error.
- **Updates are daily, on by default, and the first check is a day after install.** This keeps a fresh profile at zero requests (`zero_traffic`) and still meets "scheduled background updates". The user can turn it off. Recommended answer to the open privacy question: keep it on, since stale lists are worse for users than one request a day to the list publisher.
- **Exceptions are hosts, matched on the top-level site** (the host or a subdomain), the same site the classifier already uses as first party. The service holds requests until the settings load, so a restored session never sees ads on an excepted site.
- **A failed update is not retried until the next check.** The check time is set when an update runs, whether or not each list succeeds. A list that fails keeps its old copy and shows its error, and the next automatic try is a day later. This is on purpose: a publisher that is down or a bad list is not hit again and again. The user can click Update at any time.
- **Incognito exceptions are memory only.** An exception set from an off-the-record profile is kept in memory for that profile (the engine keys it by the profile, and each request carries the key through patch `0008`). It is not written to `state.json`, does not apply to the regular profile or another incognito session, and is dropped when the profile is destroyed. Writing it to disk would leave the incognito browsing history of the site in the regular profile. A saved exception still applies in incognito, and incognito cannot turn it back on (the page says so).
- **Blocking off also means no cosmetic hiding** (ADR 0002). `IsCosmeticFilteringEnabled` is false for an excepted site, and cosmetic queries wait for the settings like requests do.
- **The count** is per tab and per page. The proxy factory learns its frame from the hook (`patches/adblock/0008`); a `WebContentsUserData` counts and resets on the next committed document. Workers are not counted.
- **UI**: a WebUI page, `chrome://aerie-filters`, and a toolbar button, not the Settings page (a Polymer page would need a Helium-coupled settings patch). The sidebar and command bar take the button's role in M2 and M3.

## Consequences

- The browser now contacts list publishers. Their addresses are in [network-endpoints.md](../network-endpoints.md).
- The overlay adds a WebUI, a toolbar button and three hook patches (`0008`, `0009`, `0010`). None depends on a Helium patch.
- Resource id range 10270 is taken for the page's grd.
