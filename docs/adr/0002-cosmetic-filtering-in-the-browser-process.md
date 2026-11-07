# 2. Hide ad containers from the browser process, in an isolated world

Status: accepted (issue #9)

## Context

Blocked requests leave empty boxes. Cosmetic rules from EasyList (plain element hiding) fix
that. Brave does it with a renderer-side script and a Mojo interface. Aerie's first Filter
Engine keeps everything the user's pages must not influence in the browser process
([ADR 0001](0001-native-adblock-engine.md)).

## Decision

- **Where.** A `WebContentsObserver` per tab (`AerieCosmeticTabHelper`) asks the Filter Engine
  service for CSS and injects it with `ExecuteJavaScriptInIsolatedWorld`, as a constructed
  stylesheet. No renderer code, no new Mojo interface, no renderer GN target.
- **Output is CSS only.** The engine returns `selector{display:none!important}` lines, one per
  selector. It never returns script. Scriptlets, HTML filters, procedural rules and style
  injection are dropped when the lists compile.
- **Selectors are checked twice**, when the lists compile and on every stylesheet that leaves the
  engine (`is_safe_selector`), because adblock-rust is built without `css-validation`.
- **Generic rules** need the page's classes and ids. The helper reads them with a fixed script
  at `DOMContentLoaded` and at load, and sends them to the engine as lookup keys only.
- **Per-site switch.** `FilterEngineService::SetCosmeticFilteringPolicy` takes the top-level
  URL. Issue #10 installs its policy there. Default: on.
- **Domain resolver.** adblock-rust panics in cosmetic matching without one, so the core
  installs `AerieDomainResolver`, a heuristic, instead of vetting the public suffix crate.

## Consequences

- Elements added after the load event are hidden only if their class or id was on the page by
  then. A mutation observer would need a channel back from the page's world; later, if it matters.
- The hide arrives after the page starts rendering: a short flash is possible. A renderer-side
  start-of-document injection would avoid it, at the cost of a renderer component and Mojo
  interface that a compromised renderer could talk to.
- The page and the isolated world share the DOM but not JavaScript objects, so a page cannot
  replace the functions the injection uses. The Bench fixture tries it (`cosmetic_filtering`).
- One more hook patch (`patches/adblock/0007`, two lines in `TabFeatures`).
