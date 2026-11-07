# Contributing to Aerie

## Run the tests

```
python3 -m unittest discover -s tests -t . -v
```

Python 3.11 or newer. The tests need `patch`, `tar` and `zstd` on the host, and they build their own tiny source trees. They do not download anything and do not touch a build root.

## The Patch Layer

Aerie changes Chromium in three layers. Each layer is a quilt-style directory with a `series` file, and the build applies them in this order:

1. `helium-chromium/patches` (Helium core, a submodule)
2. `helium-linux/patches` (Helium for Linux, a submodule)
3. `patches` (Aerie)

Rules for Aerie's layer:

- Put Aerie-owned source in its own directories. Upstream files get only the smallest hook patch that is needed to call into it.
- Never edit the submodules. A change to Helium goes to Helium, or becomes an Aerie patch.
- Aerie patches apply with fuzz off. A context line that no longer matches is an error, not a warning.
- Aerie patches apply before any name substitution, on text exactly as Helium's patches leave it.
- Add a patch by putting the file under `patches/<area>/` and adding its path to `patches/series`. A `.patch` file that is not in the series is an error.

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

The command lints Aerie's series (header fields, missing or unlisted files, unsafe paths), then applies all three layers in build order. It exits non-zero and names every patch that fails. A patch that needs a file that a failed patch changed is reported as skipped.

It does not need a full source tree. It extracts only the files the three series touch from the pinned Chromium tarballs (after checking their sha256 against `helium.lock`) into `<build root>/check/sparse`, copies that to `<build root>/check/work`, and applies the patches there. The first run reads the tarballs and takes a few minutes. Later runs reuse the sparse tree until a series touches a new file.

Run it after you change a patch and after you bump Helium.

### Helium to Aerie names

Helium turns "Chromium" and "Chrome" into "Helium" in string files (`.grd`, `.grdp`, `.xtb`) after its patches. Aerie then runs `tools/aerie_build/substitution.py`, which turns "Helium" into "Aerie" in the text users read. Do not patch single strings by hand.

The pass keeps `helium://`, "The Helium Authors" and message descriptions. It rewrites translation ids to match the new text. It runs after Helium's translation step, because that step looks messages up by the Helium text.

To protect another span, add a rule to `RULES` in that file and add a test. A tree that was substituted with older rules must be rebuilt with `./aerie build --clean`.

### Changing the series of an existing tree

`./aerie build` records each Aerie patch it applies, with a hash, in `.aerie-patches.json` in the source tree. New patches at the end of the series apply on top of an existing tree. A patch that was edited, removed or reordered after it was applied needs `./aerie build --clean`.

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

When you move the Helium pins in `helium.lock`, also update the official
Chromium tarball pin in the `[fallback.chromium]` section of that file (URL and
sha256 from the `.hashes` file next to the tarball), and check the ledger.
