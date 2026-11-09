# 7. Use one Design Token source for Views and WebUI

Status: accepted (issue #142). The maintainer approved the #142 design and its token values on 2026-11-09. The CSS generator exists in the prototype. The C++ generator is **to build in M2**.

## Context

Aerie's sidebar, tab strip, command bar and window frame are native Views. Its settings-like surfaces, such as the Workspace editor and the Guard panel, are Lit WebUI pages ([spec #1](https://github.com/pedrosousa13/aerie/issues/1), "Views-first hybrid"). Both kinds of surface must look the same. They need the same colours, radii, spacing, type sizes, sizes and motion values.

The spec asks for a Design Tokens module that both read from. It also asks for per-Workspace themes, contrast that passes WCAG AA, and support for reduced motion and high contrast.

The #142 design pass built `prototypes/sidebar/tokens.json` to answer this. It holds:

- colour, shadow, radius, space, type and size tokens, with light and dark values;
- six themes (Vivid, Soft, Clean, OLED, High contrast, Mono) and eight Workspace colour presets;
- motion tokens (duration, easing, spring, distance, scale, opacity, timing, ratio), each with a reduced-motion value where it differs.

`gen_tokens_css.py` turns it into `tokens.css`. `check_contrast.py` checks every theme, mode and preset. The mockup, the motion spec page and the token spec page all read the same file, so they cannot drift from it.

The design records the look of a Workspace as data: `{ colour, theme, mode }` on the Workspace record in the Tab Model. The record syncs later (story 96). The tokens are the values these choices select.

## Decision

1. **`tokens.json` is the single source of Design Tokens.** Nothing else defines a colour, size or motion value for Aerie's own UI.
2. **Where it lives.** It moves from the prototype into the source tree as an Aerie-owned component, for example `overlay/components/aerie_design_tokens/` (next to `aerie_adblock` and the other `aerie_*` components). The prototype copy in `prototypes/sidebar/tokens.json` is the seed. After the move, the prototype reads the component copy or is deleted.
3. **A generator emits both outputs.**
   - CSS custom properties (`--aerie-<group>-<token>`) for WebUI, as `gen_tokens_css.py` does today.
   - C++ constants for native Views: a generated header with colour, size and motion values, and one table per theme and mode (**to build in M2**).
4. **Nobody edits generated output by hand.** Each generated file starts with a "Generated from tokens.json. Do not edit." header. CI regenerates it and fails if the checked-in copy differs.
5. **Every theme, mode and Workspace preset passes the contrast check in CI.** The check is `check_contrast.py` as it is, moved with the tokens. The minimums are 4.5:1 for text and muted text (7:1 in High contrast), 3:1 for the accent and focus ring, and 4.5:1 for status colours on their surfaces.
6. **Motion tokens carry their reduced-motion values.** A token with a `reduced` field has it in both outputs. The CSS output switches on `prefers-reduced-motion` and the in-app setting. The C++ output exposes both values and the Views code picks one from the reduced-motion state.
7. **Chromium's own surfaces follow the theme through a ColorMixer.** The generated C++ also feeds Aerie's theme colours into a `ui::ColorProvider` mixer (`ColorMixer`), so Chromium's own Views (menus, dialogs, the omnibox popup) change with the effective theme and mode. Aerie code reads the generated constants directly. The mixer is the bridge for code Aerie does not own.
8. **Per-Workspace appearance stays data.** `{ colour, theme, mode }` is stored on the Workspace record, not in the tokens file. The effective theme is resolved at runtime and selects one of the generated theme tables.

## Consequences

**Good**

- One edit changes both Views and WebUI. The motion spec and token spec pages stay true because they read the same file.
- Contrast is checked for every theme, mode and preset before a value ships. A new theme or preset cannot ship unreadable.
- Reduced-motion behaviour is a property of the token, so each surface gets it without its own switch.
- A generated header means no JSON parsing and no allocation at startup.
- Chromium surfaces match Aerie's themes without Aerie patching each of them.

**Bad, and why this is hard to reverse**

- Once Views code and WebUI pages read generated constants, changing the source format or the token names touches every surface. A rename is a repo-wide change. Names are therefore part of the design: `--aerie-<group>-<token>`, with motion groups `motion-duration`, `motion-easing`, `motion-timing`, and so on.
- The theme and Workspace-override model is stored in the Tab Model and synced later. The theme ids (`vivid`, `soft`, `clean`, `oled`, `high-contrast`, `mono`) and preset names (`dusk`, `tide`, `moss`, `ember`, `lilac`, `slate`, `sand`, `mint`) become persisted values. After sync ships, removing or renaming one needs a migration on every device.
- The generator is Aerie code to maintain, in Python for CSS and in C++ or Python for the header. The build needs it to run before compile. It must be fast and deterministic.
- CSS container queries cannot read variables, so one value, `size-hz-tab-icon-only`, is repeated in `horizontal.css`. The generator checks that the copy matches. This is a known exception.
- The prototype's per-theme override keys (`color.text`, `shadow.card`, `size.focus-ring-width`) need a stable meaning in the C++ tables. The M2 generator must define it and test it against the CSS output.

## Alternatives considered

### Chromium's ColorProvider and ColorMixer only

Views-native, and Chromium's own code already reads it. It does not cover WebUI pages, which read CSS variables. It also has no place for size, radius, type or motion tokens, nor reduced-motion values. Using it alone would leave two sources for the same colours, one for Views and one for WebUI.

This option stays in the decision as the bridge (decision 7). Aerie feeds generated values into a ColorMixer so Chromium's own surfaces follow the theme. It is not the source.

### Hand-maintained parallel CSS and C++

Simple at first and no generator. The two copies drift. The first mismatch would be a colour that differs between the sidebar and the Workspace editor. Nothing would check contrast across both. Rejected.

### Load the JSON at runtime

One file, read when the browser starts. It costs parsing and allocation on the startup path, against the performance goal and the rule that nothing animates or builds curves on a startup path. It also moves errors from build time to run time. Rejected. A generated header is free at startup.

## Open details for M2

- The exact C++ shape: one `constexpr` header with a table per theme and mode, or several headers. The naming style of the constants.
- Where the generators run (a GN action in the build, or a pre-build step) and how CI runs the contrast check.
- How the in-app reduced-motion setting reaches the generated reduced values in Views.
