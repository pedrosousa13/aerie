# Contributing to Aerie

## Run the tests

```
python3 -m unittest discover -s tests -t . -v
```

Python 3.11 or newer. The tests need `patch`, `tar` and `zstd` on the host, and they build their own tiny source trees. They do not download anything and do not touch a build root.

The C++ unit tests need a prepared source tree and a compiler: `./aerie test-cpp` builds and runs them (see the README, "C++ unit tests"). Unlike the build for the browser, it downloads one archive of Chromium's test fonts from Google storage, pinned by the sha256 in the tree's `DEPS`.

Tests for the Rust side need cargo, a C++ compiler and crates.io, so they are off by default: `AERIE_TEST_CARGO=1 python3 -m unittest tests.test_adblock_rust -v`.

## The Patch Layer

Aerie changes Chromium in three layers. Each layer is a quilt-style directory with a `series` file, and the build applies them in this order:

1. `helium-chromium/patches` (Helium core, a submodule)
2. `helium-linux/patches` (Helium for Linux, a submodule)
3. `patches` (Aerie)

Rules for Aerie's layer:

- Put Aerie-owned source in its own directories. Upstream files get only the smallest hook patch that is needed to call into it.
- Never edit the submodules. A change to Helium goes to Helium, or becomes an Aerie patch.
- Aerie patches apply with fuzz off. A context line that no longer matches is an error, not a warning.
- Aerie patches apply last, on the tree exactly as Helium's steps leave it (after Helium's domain and name substitution, translations and version). A context line that one of those steps rewrote must match the rewritten text. Lines a patch adds are not rewritten by those steps, so write them in their final form: the lint rejects an added line that matches `helium-chromium/domain_regex.list` in a file on `domain_substitution.list`, an added `.grd`/`.grdp` line that says Chrome or Chromium, a patch that edits an `.xtb` file, and rename/copy headers.
- Add a patch by putting the file under `patches/<area>/` and adding its path to `patches/series`. A `.patch` file that is not in the series is an error.

### The overlay

Aerie-owned source that is too big to live in a patch goes in `overlay/`, laid out like the Chromium tree. `./aerie build` copies it into the source tree as an Aerie layer, after the patches apply and before the Helium to Aerie substitution. The overlay only adds files: a file that already exists upstream is an error, so change upstream files with a hook patch. Files that matter only for standalone `cargo` (`Cargo.toml`, `Cargo.lock`, `target/`) are not copied. See `tools/aerie_build/overlay.py`.

The Filter Engine is the first overlay user; see [docs/adblock.md](docs/adblock.md) for its build steps and its two locks (filter lists, Rust crates).

### Branding art and the release number

Two more Aerie layers sit between the overlay and the substitution:

- **Art.** `branding/resources.txt` lists `<file in branding/art> <tree path>` pairs. The layer replaces the files that Helium's resource step copied (logo, product icons, new tab page favicon, vector icons). A destination that is not in the tree is an error. The art is exported from the sources in `branding/source/` by `tools/aerie_branding/export.py` (see `branding/source/README.md`); new art replaces the files in `branding/art/` under the same names and sizes. A test compares the destinations and image sizes with Helium's lists.
- **Release number.** `AERIE_VERSION` (three numbers) is added to `chrome/VERSION` as `AERIE_MAJOR`, `AERIE_MINOR` and `AERIE_PATCH`, and `patches/branding/0006` makes the About version use them. To make a release, change that file.

The Linux desktop entry, metainfo and icon install script are in `packaging/linux`. The Bench checks `branding_no_helium`, `branding_scheme` and `branding_version` prove the result on a build (see [docs/bench.md](docs/bench.md)).

### Dropping or adapting a Helium patch

Aerie never edits the Helium submodules, but it can skip a Helium patch: list its series entry in `patches/helium-drop.txt`, one per line, followed by `# reason`. The build and `./aerie patches check` then apply Helium's series without it. A Helium patch that only applies on top of a dropped one can be replaced, at the same place in the series, by a copy in `patches/helium-adapted/<entry>` listed in `patches/helium-adapt.txt` (same format; the copy needs the Aerie header). The check fails if a listed entry is no longer in Helium's series, so a Helium bump surfaces it, and a test fails without a row for each entry in [docs/helium-dependency-ledger.md](docs/helium-dependency-ledger.md). A tree patched with other drops needs `./aerie build --clean`.

**Re-cutting an adapted patch** (after `./aerie patches check` says "Helium's patch changed since it was adapted", or when a new drop breaks a Helium patch). Each adapted entry in `patches/helium-adapt.txt` pins the sha256 of Helium's original. To re-cut:

1. Build a private sparse tree with Helium's series (minus the drops, with the other adapted copies) applied up to, not including, the entry.
2. Apply Helium's original with rejects (`patch -p1 --forward --reject-file=...`). The hunks that fail are the ones whose context needs the dropped code. Check that each one only adds lines (a hunk that removes lines needs thought).
3. Put each rejected hunk's added lines after the nearest context line that still exists.
4. `diff -u` the tree before and after into `patches/helium-adapted/<entry>`, add the Aerie header, and put the sha256 of the new Helium original in `helium-adapt.txt`.
5. Run `./aerie patches check`. It also compares the copy with the original: every added or removed line must be one of the original's, every line of hunks that do not mention uBlock Origin must still be there, and no changed line may mention it.

The re-cut changes where hunks go, never what they add. Sections of Helium's `deps.ini` that Aerie does not download are in `tools/aerie_build/helium_deps.py`.

### Patch header

Text before the first `---` line of a patch is ignored by `patch`. Every Aerie patch starts with this header there:

```
Aerie-Purpose: <one sentence: what the patch does and why>
Aerie-Upstream-Area: <upstream path or component the patch touches>
Aerie-Kind: <optional: hook | tracer | branding | backport>

Free text can follow after a blank line.
```

`Aerie-Purpose` and `Aerie-Upstream-Area` are required and must not be empty. A long value can continue on lines that start with a space. The build and the check command both reject a patch without them.

### Check that the series applies

```
./aerie patches check
```

The command lints Aerie's series (header fields, missing or unlisted files, unsafe paths, added lines that Helium's substitutions would have rewritten), then follows the build: it applies Helium's two series, runs Helium's domain substitution, name substitution and version steps on the sparse tree, and only then applies Aerie's patches. So a patch is judged on the text the build gives it. It exits non-zero and names every patch that fails. Offsets and fuzz count as failures. A patch that needs a file that a failed patch changed is reported as skipped. If Helium's own patches fail, Aerie's are not tried.

It does not need a full source tree. It extracts only the files the three series touch, plus `OWNERS`, `chrome/VERSION` and `tools/grit/grit/` that the steps need, from the pinned Chromium tarballs (after checking their sha256 against `helium.lock`) into `<build root>/check/sparse`, copies that to `<build root>/check/work`, and runs there. The first run reads the tarballs and takes a few minutes. Later runs reuse the sparse tree until a series touches a new file. Helium's i18n step is not run (it needs every translated string file and only edits `.xtb` files, which patches may not touch).

If a prepared source tree exists in the build root, the command also tries Aerie's patches against it, without writing to it, and reports the result like `./aerie build --dry-run-layers`.

The command ends with the source gates: checks on the patched sparse tree that
need no build. They fail if a Helium hostname is back in source (outside a short
allowlist with a reason for each path), if the default search list changed, or if
search suggestions, secure DNS, crash upload or Helium services are no longer off, if the
FedCM DNS resolver prefix is no longer empty, if `kHttpsFirstDialogUi` is on again
(the full HTTPS-only warning page needs it off), or if link prefetch no longer follows
the preloading pref (`dns_prefetching_enabled` set in `OverrideWebPreferences` and
honoured by `PrefetchIfNeeded`), or if Widevine is no longer opt-in (`widevine_opt_in`: it must be
registered only through `aerie_widevine::RegisterWhenEnabled`, the component update URL and the
update check and ping must go through the opt-in so Google is sent only Widevine, and the pinned
Widevine signing-key hash `kWidevineSha2Hash` must be unchanged), or if the per-site JavaScript JIT toggle is no longer whole (the `js_optimizer` gate: the content setting defaults to allow, its settings route exists, and a process never serves two optimizer modes), or if the Helium services user interface is back (the `services_ui` gate: no services page, route or row in Settings, nothing opens the removed route, the Languages page does not point at Helium servers, the extension proxy dialog text is a grd message, and the extension proxy address is a row of the privacy page), or if audio noise is seeded from anything but the site, session and sample coordinates again (the `audio_noise_stable` gate: no per-render or per-read counter), or if the graphics card is a fingerprint surface again (the `gpu_masking` gate: WEBGL_debug_renderer_info goes through the one masking helper with no second SpoofWebGLInfo branch, and WebGPU adapter info is built from values that are empty while masked), or if a kind of worker loses its fingerprinting noise tokens (the `worker_noise_tokens` gate: a dedicated worker takes its creator's tokens, a shared worker and a service worker get tokens the browser derived from the top-level site of the storage key and only while protection is on, never the session tokens, and WebGL in a worker follows its token instead of always masking). Also the tokens are keyed on the storage partition (the Workspace) as well as the site (the `workspace_noise_partition` gate: a navigation, a shared worker and a service worker each pass their partition to `GetTokens`, and the key includes it). Also a window opened with `window.open()` must get its opener's tokens when it is created, or the opener reads the real values through its initial empty document (the `popup_noise_tokens` gate: `CreateNewWindowReply` carries the tokens, the browser sets them on the new page and sends them, and the renderer puts them in the new view's `CreateViewParams`). Also `readPixels` is noised only on the default framebuffer (the `webgl_readpixels` gate: `ReadPixelsHelper` decides with `ShouldNoiseReadPixels(/*default_framebuffer=*/!framebuffer, ...)` and the helper takes the canvas token and does nothing without it). The same gates
run on a real tree with `./aerie patches gates [--tree DIR]`. They live in
`tools/aerie_build/treegate.py`; the endpoints they protect are listed in
[docs/network-endpoints.md](docs/network-endpoints.md).

### Helium to Aerie names

Helium turns "Chromium" and "Chrome" into "Helium" in string files (`.grd`, `.grdp`, `.xtb`) after its patches. Aerie then runs `tools/aerie_build/substitution.py`, which turns "Helium" into "Aerie" in the text users read. Do not patch single strings by hand.

The pass also turns `helium://` into `aerie://` (the scheme itself is renamed by `patches/branding/0002`). It keeps message descriptions. It rewrites translation ids to match the new text. It runs after Helium's translation step, because that step looks messages up by the Helium text.

Aerie is derived from Helium (GPL-3.0) and Chromium (BSD), so their copyright notices stay. The pass turns the user-visible copyright holder "The Helium Authors" into "The Aerie Authors, The Helium Authors and The Chromium Authors". It does this with a protected span: the first rule in `RULES` marks the kept "Helium" with `_KEEP`, the name rule skips marked spans, and the last rule removes the marks. The pass only reads string files and only changes message text, so copyright and licence notices in source files, licence files and XML comments are never rewritten. `patches/branding/0001` sets the `COPYRIGHT` line to the same credit. To protect another span, mark it in an earlier rule in `RULES` the same way and add a test in `tests/test_substitution.py`. Every rule is also used to decide which files need parsing, so a new rule needs no other change. A changed rule needs no `--clean`: the pass always starts from the pristine text (see below).

### Changing the Aerie layers of an existing tree

A clean build takes about 2.5 hours and ccache helps little, so `./aerie build` keeps the source tree and `out/Default`, and re-applies the Aerie layers (the patch series, the overlay, the branding art, the release number and the Helium to Aerie substitution) on every run. No `--clean` is needed to add, edit, reorder or remove a patch or to change a substitution rule.

How it works (`tools/aerie_build/layers.py`):

- Before a layer first touches a file, its pristine bytes are saved in `<build root>/aerie-pristine/<tree>/files/`, with a `manifest.json`. A file that a layer creates is recorded as "did not exist".
- Each build works out the final content of every file the layers touch from the pristine bytes, in a staging directory. It writes a file only if its bytes differ from the tree, through a temp file and an atomic rename. Unchanged files keep their mtime, so ninja does not rebuild them.
- A file that no layer touches any more is restored to its pristine bytes (or deleted, if it did not exist before).
- A `journal.json` exists while a run changes files. If a run is interrupted, the next run says so, removes leftover `*.aerie-tmp` files and finishes the job. The tree is never written before its pristine copy is saved.
- Rewriting a string file (`.grd`) regenerates resources and a translation file (`.xtb`) re-runs its repack, so a changed rule or patch on those files costs a partial rebuild. A run with no change writes nothing.

To see what the next build would change, without writing anything:

```
./aerie build --dry-run-layers
```

`./aerie build --clean` deletes the source tree and the pristine snapshot. Use it after a Helium bump. A damaged snapshot also needs it. `./aerie build` refuses, and says so, in these cases, because the tree was processed in a way that cannot be undone:

- Helium's own inputs changed since the tree was prepared: the patch series of helium-chromium or helium-linux, `domain_regex.list`, `domain_substitution.list`, `pruning.list`, the translations, or any `helium-drop.txt` or `helium-adapt.txt` in the repo. A digest of them is stored in the tree as `.helium-inputs.sha256`. A tree made before that file existed adopts the current digest.
- An older Aerie build changed the tree in place (it has `.aerie-patches.json`, `.aeriesub.stamp` or `.aerie-fingerprints.json`), so no pristine copies exist.

One build at a time can re-apply layers for a tree (a lock file next to the snapshot enforces it). `./aerie prune` removes the snapshots of old trees.

`./aerie build`, `./aerie prune` and `./aerie bench` also take a build-tree lock, which the local CI shares (see [docs/ci.md](docs/ci.md)). A build train that runs a build and then a Bench should hold the lock across both: `./aerie locked -- sh -c './aerie build && ./aerie bench'`.

To add a layer that touches tree files, implement `layers.Layer` (`apply(workspace)`: read with `workspace.read`, stage results with `workspace.put`) and add it to `aerie_layers()` in `tools/aerie_build/pipeline.py`.

## Testing the Views UI (seam B)

Views UI (the sidebar, the Guard bubble and anything else drawn by Views or opened from it) is tested with
Chromium browser tests and interactive UI tests. Chromium's own `browser_tests` and `interactive_ui_tests` link
every browser test of Chromium and need test data that Helium prunes, so Aerie has two test-only binaries of its
own. They link Aerie's test files and `//chrome/test:test_support_ui`, and nothing in the shipped browser links
them (`patches/tests/0002` only makes GN load their build file).

| Binary | For | Runs |
| --- | --- | --- |
| `aerie_browser_tests` | `IN_PROC_BROWSER_TEST_F` cases: a window, a tab, a bubble, a WebUI page | with `--ozone-platform=headless`, `--no-sandbox` and `--disable-gpu`, no X server |
| `aerie_interactive_ui_tests` | cases that need focus or real key and mouse events | under `xvfb-run` (package `xvfb`), with `--no-sandbox` and `--disable-gpu` |

```
./aerie test-cpp --target aerie_browser_tests --gtest-filter='Aerie*'
./aerie test-cpp --target aerie_interactive_ui_tests
```

`--disable-gpu` is there because a 2D canvas reads back blank in the test processes without it. `--no-sandbox` is there because the build machine's AppArmor allows user namespaces only for the `aerie` browser binary; the shipped browser keeps its sandbox. The maintainer accepted this for the two UI test binaries only, on 2026-11-10, for that reason. A plain `./aerie test-cpp` runs the unit test binaries only. The nightly runs both UI binaries with
`--gtest-filter=Aerie*` after the Bench (see [docs/ci.md](docs/ci.md)).

Conventions:

- **Where the files go.** Next to the code they test, as `aerie_guard_browsertest.cc` sits in
  `overlay/chrome/browser/ui/webui/aerie_guard/`. Browser tests are `*_browsertest.cc`, interactive tests
  `*_uitest.cc`. Put them in a `testonly` `source_set("browser_tests")` (or `"interactive_tests"`) in the
  directory's `BUILD.gn`, then list that source set in `deps` of `aerie_browser_tests` (or
  `aerie_interactive_ui_tests`) in `overlay/chrome/test/aerie/BUILD.gn`. Do not add it to Chromium's
  `browser_tests`; the Guard tests are in both for now, through `patches/adblock/0021`.
  Both source sets set `defines = [ "HAS_OUT_OF_PROC_TEST_RUNNER" ]`; `browser_test.h` is an error without it.
- **Names.** The test suite (the fixture class) starts with `Aerie`, for example `AerieGuardBrowserTest`. The
  nightly and the filter `--gtest-filter=Aerie*` select tests by that prefix. A test without it never runs in CI.
- **Reduced motion.** A test that checks the reduced-motion version of an animation forces it with
  `gfx::AnimationTestApi::SetPrefersReducedMotionForTesting(true)` (`ui/gfx/animation/animation_test_api.h`) and
  keeps the returned resetter alive for the whole test. Run the same steps once with `false` to check the full
  version, so the test shows that the switch changes the result.
- **Reading animations.** Use `ui::LayerAnimatorTestController`
  (`ui/compositor/test/layer_animator_test_controller.h`): `GetRunningSequence()` tells whether an animation
  runs, and `Step(duration)` moves it forward by a known time. Do not use real sleeps.
- **Focus and keys.** Only `aerie_interactive_ui_tests` may rely on focus or on key and mouse events from
  `ui_controls`. `aerie_interactive_smoke_uitest.cc` is the smallest example.
- **Downloads.** The binaries use the test fonts (https, sha256 from `DEPS`) and three sets of Chromium test files that
  `./aerie test-cpp` fetches (https, pins in `helium.lock` under `[test_data.webui]`, `[test_data.pdf]` and
  `[test_data.extension_api_unittest]`), all kept in `downloads/`. A test must not add another download.
- **Web UI test libraries.** An Aerie test cannot load the `chrome://webui-test` libraries that `patches/tests/0008`
  drops from the bundle: cr_elements, settings, signin, history, new_tab_page, customize_chrome and extensions.

### Updating the test data pins

`./aerie bump` (`rewrite_lock`) does not touch the `[test_data.*]` tables, and the build refuses a pin whose
`chromium_version` is not the tree's. After a Chromium bump, change in each of the three tables `chromium_version`,
the Chromium version inside `url` (`.../refs/tags/<version>/...`) and `sha256`. The server stamps the archive with the
request time, so `sha256` is not the archive's hash. It is the `content_digest`: download the archive from the new
`url`, then run, from the repository root,
`python3 -c "from pathlib import Path; from tools.aerie_build import test_webui_data as t; print(t.content_digest(Path('ARCHIVE.tar.gz'), 'webui'))"`
(use `pdf` or `extension_api_unittest` as the second argument for the other tables). Check with `./aerie test-cpp --target aerie_browser_tests`.

## Hardening defaults

The privacy and security defaults are one table, `tools/aerie_hardening/controls.json`. The
settings page markup (`patches/hardening/0004-...`), `docs/hardening.md` and the Bench checks all
follow it. After you edit the table run `./aerie hardening generate`; `./aerie hardening check`
and the unit tests fail while a generated file is stale. A control needs at least one Bench check
with a negative control. See [docs/hardening.md](docs/hardening.md).

## Design Tokens

Every colour, radius, space, type, size and motion value of Aerie's own interface is in one file, `overlay/components/aerie_design_tokens/tokens.json` ([ADR 0007](docs/adr/0007-single-design-token-source.md)). Do not type such a value anywhere else. The build generates `aerie_tokens.css` (WebUI) and `aerie_tokens.h` (Views) from it. Token names are persisted values later: do not rename one.

After you edit `tokens.json`, regenerate the checked-in copy for the mockup pages:

```
C=overlay/components/aerie_design_tokens
python3 $C/gen_tokens_css.py --out prototypes/sidebar/tokens.css --check-horizontal prototypes/sidebar/horizontal.css
python3 $C/check_contrast.py
```

`tests/test_design_tokens.py` fails if that copy is stale, if any theme, colour mode or Workspace preset fails the contrast check, or if a C++ theme table differs from the CSS. A change to a token value needs its own pull request and a passing contrast check.

## Helium dependency ledger

Every Aerie patch is a change on top of Helium. If it only applies, builds or
works because a Helium patch is there, record that in
[docs/helium-dependency-ledger.md](docs/helium-dependency-ledger.md) **in the
same pull request**. A pull request that adds such a patch without a ledger
row is not ready to merge.

Add a row when your patch:

- edits lines that a Helium patch added or changed;
- calls code, flags or resources that a Helium patch adds;
- assumes a setting or behavior that a Helium patch sets; or
- changes a Helium-owned file (a list, a `flags` file or a utility script).

Give the Helium patch path exactly as in the submodule's `patches/series`,
say why you need it, and say how to port it to ungoogled-chromium
(`copy`, `rewrite` or `drop`). The tests fail if a listed patch is not in the
pinned Helium series. When a Helium bump removes or renames a patch, fix the
ledger in the same pull request as the bump.

If your patch does not depend on a Helium patch, say so in the pull request
description. The ledger is how Aerie moves to another base if Helium stops, so
a missing row costs real work later. See
[docs/helium-fallback.md](docs/helium-fallback.md).

## Updating Helium

`./aerie bump` moves the pins and rewrites `helium.lock` for you, and a daily job opens the
pull request (see [docs/helium-bump.md](docs/helium-bump.md)). To move the pins by hand instead,
also update the official
Chromium tarball pin in the `[fallback.chromium]` section of that file (URL and
sha256 from the `.hashes` file next to the tarball), and check the ledger.

## Updating the benchmark pins

The Bench's `perf` check runs Speedometer and JetStream from archives pinned in the `[bench.*]` tables of `helium.lock`. They are
downloaded into the build root and never committed. To move one, change its `commit`, `url`, `archive`, `sha256` and (if it
changed) `license`, run `./aerie bench --download-benchmarks` on a clean build root, run `./aerie bench --check perf` and check that both scores
come out, update the pin table and the licence note in [docs/bench.md](docs/bench.md), and record a new perf baseline after the merge (scores of
different pins do not compare). The pins do not depend on a Helium patch, so they need no row in the ledger above; say that in the pull request.
