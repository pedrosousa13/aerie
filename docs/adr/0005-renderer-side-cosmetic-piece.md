# 5. Allow a small renderer-side piece for cosmetic filtering

Status: accepted (issue #68). Amends [ADR 0002](0002-cosmetic-filtering-in-the-browser-process.md).

## Context

ADR 0002 kept all of cosmetic filtering in the browser process and injected a constructed
stylesheet from an isolated world. Issue #44's triage found two gaps that this cannot close:

- The sheet is an author-origin sheet. A page can empty `document.adoptedStyleSheets`, or override a
  hide with an inline `!important` style, which anti-adblock scripts do.
- An element added after the load event is hidden only if its class or id was on the page when the
  browser read it. Reading again needs a way for the page's world to tell the browser.

Brave Shields is the reference (brave-core, MPL-2.0; Aerie wrote its own code and copied none). It
inserts the hide rules as a user-origin sheet from the renderer and watches the DOM for late
elements. A user-origin `!important` rule beats an author `!important` rule, and the page cannot
see or remove the sheet. The maintainer decided on 2026-11-08 to follow it.

## Decision

Cosmetic filtering gets one renderer piece, `AerieCosmeticAgent` (`components/aerie_adblock/renderer`,
two hook patches: `patches/adblock/0012` creates the agent for each frame, `patches/adblock/0013` lets its
reports reach the tab's helper in the browser). Everything else stays in the browser process: the engine, the
lists, the policy and the exceptions, and the choice of what is hidden.

- **User-origin sheets.** The browser sends CSS text over a new Mojo interface; the agent inserts it with
  `WebDocument::InsertStyleSheet(..., blink::WebCssOrigin::kUser)`. A frame has at most one page sheet and one
  generic sheet. The page sheet is replaced whole (an empty one clears it). The generic sheet is appended to: the
  passes at DOM ready, at load and for late elements all append, and a line that is already in the sheet is
  not added again (dedup), so the passes can come in any order.
- **Bounded re-insertion.** To add lines the agent removes the generic sheet and inserts it again (still exactly
  one generic sheet), and Blink restyles each time, for a sheet of up to 1 MiB. So the first insertion is at
  once, later ones wait until 250 ms after the last, and a document gets at most 64. Rules that arrive in
  between wait in the agent's text and go in with the next insertion.
- **Late elements.** After DOMContentLoaded the browser runs a fixed script in the Aerie isolated world. It
  watches DOM mutations and reports class names and ids it has not reported before, in coalesced batches.
  The agent passes them on, and the browser returns the matching generic rules. The agent adds them to the
  generic sheet. A reported token is only a lookup key, as at load. The script drops tokens that are not ASCII
  (the engine takes ASCII only, and the engine skips such a token on its own, so one bad token never voids a
  report). It reads a bounded amount of mutation work at a time and queues the rest, and a token leaves its
  queue only when the renderer accepted the report. The cost is bounded, and fair only under those limits: junk
  classes delay an ad's class, but a hostile page can exhaust the queue of waiting records, the seen-token limit,
  the reports and the insertions of a document, and then nothing more of it is hidden.
- **Where the code runs.** The browser's per-tab helper still decides everything. The renderer never asks
  "should I hide this".

### Limits on the renderer piece

These are the terms of the amendment. A change that breaks one needs a new ADR.

1. **It only applies selectors the browser process sends.** The sheet text comes from the engine in the
   browser, whose selectors were checked twice (ADR 0002). The renderer adds no selector of its own.
2. **No network access.** The target depends on no `//net` or network service code. The agent has no URL
   in its interface.
3. **It parses no filter list.** No list, rule syntax or engine code is in the renderer. It does not parse
   the CSS either: it hands the text to Blink.
4. **Its interface is narrow.** Browser to renderer: `SetRules(kind, css, append)`. Renderer to browser:
   `LateTokens(classes, ids)`, two strings that are lookup keys. No URLs, no handles, no replies. A
   compromised renderer can send tokens and nothing else. The browser treats them as untrusted: it
   checks their size (an oversized report is a bad message that ends the renderer), applies a rate and count
   bound per frame and a token bucket per tab, re-reads the policy and the per-site exception, and never puts them in a
   script. A report the browser's gate refuses is lost; the gates are looser than the renderer's, so only a
   renderer that ignores its own gate gets there.
5. **Everything is bounded.** Sheet size (the core's 1 MiB cap), token list size, tokens per document,
   mutation records and elements read per turn, the queue of waiting records, elements read per document, one
   report per interval, a cap on reports per document, and the re-insertion rate above. One header,
   `common/cosmetic_limits.h`, holds the numbers; the Rust core's caps are checked against it. The gate that
   judges a report (`common/late_report_gate.h`) is used on both sides: in the renderer, and in the browser
   for each frame. The browser also has one token bucket for all the frames of a tab: a burst of 64 reports, far
   above what many honest frames send at once (each sends one per 200 ms at most), then one more every 25 ms. It
   has no lifetime cap, so a frame that reloads itself in a loop can only slow the others for the moment, and it
   is checked before the frame's gate and taken from after it, so a report refused by one costs the other
   nothing. The cap is per tab, not per frame origin: the per-frame gate already bounds each frame, and the
   bucket bounds the tab's total rate, which is simpler than a map of origins and a policy for them.
6. **It respects the off-switch.** The browser starts the late watch only where cosmetic filtering is on, and
   re-checks on every report. A site with a per-site exception (issue #10) gets no sheet and no watch. A sheet
   that is already in a page stays until the next navigation after the user turns blocking off or adds an
   exception; only the late reports stop at once.
7. **No procedural or scriptlet filters.** Those stay dropped when the lists compile. This amendment adds no
   script that a list can influence.

## Consequences

- Pages can no longer remove a hide or override it, short of a Blink bug.
- Elements inserted late are hidden after: the script's flush delay (250 ms), up to one renderer interval
  (200 ms) if a report was just sent, the engine round trip in the browser, and the agent's re-insertion wait (up
  to another 250 ms). That is normally under 700 ms, and more under a flood. Both gates (per frame and per tab)
  and the cap of 64 insertions mean a page that mutates without end stops being hidden after its limits.
- Aerie has renderer code and one Mojo interface for the first time in this area. The attack surface is
  the two methods above. A renderer that is already compromised gains the ability to ask the browser for
  generic hide rules, which are public list data.
- A user-origin sheet is not in `document.adoptedStyleSheets` and not in `document.styleSheets`. The Bench
  counts the generic rules through the DevTools CSS domain instead.
- Two more hook patches: `patches/adblock/0012` (one line in `ChromeContentRendererClient::RenderFrameCreated`
  and one GN dependency) and `patches/adblock/0013` (one binder call in
  `chrome_content_browser_client_receiver_bindings.cc` and one GN dependency, so a frame's report reaches its
  tab's helper). The renderer target adds the sources of `components/aerie_adblock/renderer`.
- ADR 0001's rule that no renderer target depends on Aerie's engine targets still holds: the visibility of
  the engine, service and list targets is unchanged. Only `components/aerie_adblock/common` and the renderer
  target are reachable from the renderer.
