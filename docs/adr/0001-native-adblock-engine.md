# 1. Block ads natively with adblock-rust, in the browser process

Status: accepted (issue #8). Partly superseded by [ADR 0003](0003-user-managed-filter-lists.md) (issue #10): "no user controls" and "the build never fetches" now apply to the shipped lists only.

## Context

Helium ships uBlock Origin as a built-in component extension. Aerie wants
blocking that works without an extension, cannot be turned off by a page, and
stays when Manifest V2 support is the user's choice.

## Decision

- **Engine.** Brave's adblock-rust, unmodified, MPL-2.0. Aerie writes its own
  Rust/C++ bridge and request proxy, in the shape of Brave's design. It copies
  no brave-core code.
- **Where.** A proxying `URLLoaderFactory` in the browser process, added in
  `ContentBrowserClient::WillCreateURLLoaderFactory`. Not a renderer throttle (a
  compromised renderer could change it) and not the network service (the engine
  and lists would have to be pushed there).
- **Lists.** EasyList and EasyPrivacy only, under their GPL-3.0 option, committed
  in the repo and pinned by sha256 (the build never fetches; easylist.to is the
  recorded source). List management is issue #10.
- **One engine per browser process.** Per-profile engines can come with
  Workspaces.
- **Startup.** Requests wait until the engine is ready. The engine is compiled
  at build time and loaded from a serialized form, so the wait is short. If it
  cannot be loaded the browser crashes with a `CHECK`: the engine ships inside
  the build, so a failure means a broken build, and failing open is not allowed.
- **Lifetime.** The service is owned by a `ChromeBrowserMainExtraParts`, not a
  singleton.
- **First party** is the top-level site, from the `IsolationInfo`.
- **No user controls** in this issue.
- **Crates.** `gnrt vendor`, with a committed lock and a hash check of every
  added crate against crates.io (`rust/crates.lock.toml`).
- **Not covered:** WebSockets and main-frame navigations.

## Consequences

- Aerie-owned source is large for a patch layer, so it lives in `overlay/` and
  is copied into the tree (CONTRIBUTING.md); upstream files get only hook
  patches.
- List updates are reviewable commits (about 3.6 MB each, git deltas them well).
  Builds never break because upstream moved.
- The crate lock goes stale at every Chromium bump, on purpose.
- A panic in Rust aborts the browser (`panic=abort`). The core crate never
  panics on any input and is tested with garbage, huge lines and regex bombs;
  lists are pinned and checked at build time.
- Redirects are judged hop by hop. Each judged request gets a per-request
  loader wrapper (`AdblockProxyingURLLoader`) that judges the target when the
  client answers a redirect with `FollowRedirect()`, with the first URL's
  top-level-site context. The cost is one extra Mojo hop for judged requests;
  requests that are never judged skip the wrapper.
