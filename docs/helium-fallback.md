# If Helium disappears

Aerie builds on Helium. Three things keep Aerie alive if Helium stops
publishing, loses its tarball host, or deletes its repositories:

1. [Private mirrors](#mirrors) of Helium's source repositories, synced daily.
2. A [build fallback](#official-chromium-tarball-fallback) that fetches the
   official Chromium tarball when Helium's tarball host is down.
3. A [plan](#plan-move-to-ungoogled-chromium) to rebase onto ungoogled-chromium,
   with the [dependency ledger](helium-dependency-ledger.md) as the list of
   what to carry over.

## Mirrors

| Upstream | Private mirror |
| --- | --- |
| https://github.com/imputnet/helium | https://github.com/pedrosousa13/helium-mirror |
| https://github.com/imputnet/helium-linux | https://github.com/pedrosousa13/helium-linux-mirror |

Each mirror holds every upstream branch and tag, and only ever adds to them. The mirrors have GitHub
Actions turned off, so Helium's own workflows do not run in them.

`.github/workflows/sync-mirrors.yml` runs `tools/sync-mirrors.sh` every day at
03:17 UTC, and on demand (`gh workflow run sync-mirrors.yml -R
pedrosousa13/aerie`). The script fetches upstream first and pushes only after
a good fetch with at least one branch. If upstream is gone, the mirror stays
as it was. A branch or tag that upstream deletes stays in the mirror. A branch
that upstream rewrites is updated, and its old tip is first kept as
`refs/backup/<date>/<branch>`. A tag that upstream moves is not moved in the
mirror; the new target is kept as `refs/backup/<date>/moved-tags/<tag>`. The GitHub schedule only runs from the default branch of
the Aerie repository.

You can run the same script by hand. It pushes over ssh with your own git
credentials:

```sh
tools/sync-mirrors.sh
```

### One-time setup (maintainer only)

The workflow pushes to the mirrors with a token stored as the repository
secret `MIRROR_PUSH_TOKEN`. Nobody can create it for you.

1. On GitHub open Settings, Developer settings, Personal access tokens,
   Fine-grained tokens, Generate new token.
2. Resource owner: `pedrosousa13`. Repository access: only
   `helium-mirror` and `helium-linux-mirror`.
3. Repository permissions: Contents: Read and write. Workflows: Read and write
   (Helium's branches contain workflow files, and GitHub refuses to update
   them without this).
4. Set the longest expiry GitHub allows and put the date in your calendar. When
   the token expires the workflow fails with a push error.
5. Store it:
   ```sh
   gh secret set MIRROR_PUSH_TOKEN -R pedrosousa13/aerie
   ```
6. After this branch is merged, run the workflow once and check that it
   passes: `gh workflow run sync-mirrors.yml -R pedrosousa13/aerie`.

Until step 5 is done the scheduled run fails with
`secret MIRROR_PUSH_TOKEN is not set`. The mirrors already hold the state of
the day they were created.

### Using a mirror

To build from a mirror, point the submodules at it:

```sh
git submodule set-url helium-chromium git@github.com:pedrosousa13/helium-mirror.git
git submodule set-url helium-linux git@github.com:pedrosousa13/helium-linux-mirror.git
git submodule sync && git submodule update --init helium-chromium helium-linux
```

The pinned commits in `helium.lock` stay the same. `./aerie build` does not
use the nested `helium-chromium` submodule inside `helium-linux`.

## Official Chromium tarball fallback

Normally `./aerie build` takes the Chromium source from Helium's prepared
tarballs at `tarballs.imput.net`. With `--source auto` (the default), the
build first sends a HEAD request for the base tarball. If the host does not
answer with a 2xx status, the build fetches the official Chromium release
tarball for the same Chromium version instead:

```
https://commondatastorage.googleapis.com/chromium-browser-official/chromium-<version>.tar.xz
https://commondatastorage.googleapis.com/chromium-browser-official/chromium-<version>.tar.xz.hashes
```

| Option | Effect |
| --- | --- |
| `--source auto` or `AERIE_SOURCE=auto` | Probe Helium's host; fall back if it is down. Default. |
| `--source helium` | Use Helium's tarballs only. |
| `--source official` | Use the official tarball without probing. |

If Helium's host goes down in the middle of a download, the build stops with
an error in `--source helium` mode. In `auto` mode the build falls back to the
official tarball by itself. A tree that is already unpacked keeps its source:
asking for the other one with `--source` stops the build and tells you to run
`./aerie build --clean --source <source>`.

### Verification

The fetch fails closed. Nothing is unpacked unless all of these hold:

1. The `.hashes` file Google publishes next to the tarball has exactly one
   sha256 line for the file.
2. That sha256 is the same as the one pinned in the `[fallback.chromium]`
   section of `helium.lock`. If it is not, the tarball is not downloaded.
3. The downloaded tarball has that sha256. If it does not, the file is
   renamed to `*.rejected` and the build stops.

A tarball already in `<build root>/downloads/` is checked again before it is
used. Google publishes checksums, not signatures. The pin in `helium.lock` is
the stronger check: the published file alone would not catch a tarball and a
`.hashes` file replaced together.

### What the official tarball lacks

Helium's prepared tarballs (built by
[imputnet/chromium-tarballs](https://github.com/imputnet/chromium-tarballs))
are a `gclient sync` plus `gclient runhooks` of the same Chromium tag. The
official tarball is the source tree only.

**Status: an official-source build currently stops before configure.** The
toolchain layer is not built yet, so `./aerie build --source official` fetches,
verifies and unpacks the tarball, runs the same prune, patch and substitution
steps, and then stops with a list of the missing toolchain paths. Nobody has
yet run Helium's patches against an official-tarball tree, so it is not known
that they apply. Proving a full official-source build is open work.

The official tarball does not contain what `runhooks` and the DEPS `gcs` and
`cipd` entries download. Aerie needs these paths in the source tree:

| Path | What it is |
| --- | --- |
| `buildtools/linux64/gn` | `gn` |
| `third_party/siso/cipd/siso` | `siso`, the build tool behind `autoninja` |
| `third_party/llvm-build/Release+Asserts/bin/clang` | Chromium's clang |
| `third_party/rust-toolchain/bin/rustc` | Chromium's Rust toolchain |
| `third_party/node/linux/node-linux-x64/bin/node` | Node |
| `build/linux/debian_bullseye_amd64-sysroot/` | Debian sysroot |

Helium's `prune_binaries.py` lists more (`BUILD_DEPENDENCY_PATHS`: esbuild,
rollup, typescript, ninja, gperf, cpython3 and others).

With an official-tarball tree, `./aerie build` fetches, unpacks, prunes,
patches and applies the substitutions. Then, before it configures, it checks
the paths above and stops with the list of missing ones. To provide them:

1. Run `scripts/chromium_tarballs.py prepare --version <chromium version>` and
   `package` from `imputnet/chromium-tarballs` (GPL-3.0, Python 3.11 and
   `zstd`). It needs Google's hosts and `git` and builds the host layer
   `chromium-<version>-linux-x64.tar.zst`. This is the same step that
   produces Helium's own layer.
2. Unpack that layer (its files sit under one `chromium-<version>/` root, the same as Helium's layers) over the source tree:
   `tar --zstd -xf chromium-<version>-linux-x64.tar.zst -C <source tree> --strip-components=1`
3. Run `./aerie build` again. The patch steps are stamped and do not repeat. (Untested; see Status above.)

### What was tested

Unit tests (`python3 -m unittest discover -s tests -t . -v`) cover the probe,
the fallback choice with Helium's host blocked, the `.hashes` parser, a
checksum mismatch, a published checksum that differs from the pin, and a
corrupt cached file. The live official URL and checksum file were checked with
a HEAD request, a ranged request for the first 1 MiB (valid xz header), and
by parsing the real `.hashes` file against the pin. The 4.5 GB tarball was
not downloaded and its patches were not applied. The first real use of the
fallback is the first test of patch application on it, and the toolchain
steps above have never been run.

### Not covered by this fallback

`deps.ini` assets (uBlock Origin build, onboarding page, search engine data)
come from `imputnet` GitHub releases, not from the tarball host. If imputnet
deletes them, the build stops on the missing download. Keep copies of the
pinned files (listed with their sha256 in `helium.lock`) if that risk matters.

## Plan: move to ungoogled-chromium

When to start: Helium has not released for the Chromium stable version that
fixes a known security issue within two weeks, the Helium repositories are
deleted or archived, or the maintainers say they are stopping.

1. **Freeze.** Check the mirrors are current (`git ls-remote` on each mirror
   against `helium.lock`'s commits). Point the submodules at the mirrors
   ([Using a mirror](#using-a-mirror)). Aerie still builds the pinned release.
   Build with `--source official` if `tarballs.imput.net` is gone.
2. **Hold the Chromium version.** A frozen Helium has no Chromium updates.
   Take security fixes only if you can rebase; until then do not ship builds
   with a Chromium version that has known critical fixes.
3. **Add ungoogled-chromium.** Add
   [ungoogled-software/ungoogled-chromium](https://github.com/ungoogled-software/ungoogled-chromium)
   as a submodule at the tag for the target Chromium version (the tag
   `157.0.8103.38-1` matches today's pin). Its `patches/`, `pruning.list`,
   `domain_regex.list`, `domain_substitution.list`, `flags.gn` and `utils/`
   have the same layout as `helium-chromium/`. Helium's `utils/` and the
   `patches/ungoogled-chromium/` directory are forks of these. Both are
   BSD-3-Clause.
4. **Switch `./aerie build`.** In `tools/aerie_build/pipeline.py`, replace the
   two Helium submodule paths with ungoogled-chromium's, drop the Helium-only
   steps (`name_substitution`, `i18n`, `helium_version`, `resources`), and keep
   the official tarball fetch. Use the Linux packaging notes in
   `ungoogled-chromium` for the toolchain. Update `helium.lock` (version,
   commit, tarball hash) and the tests.
5. **Carry Aerie's patches over, using the ledger.** For each row in
   [helium-dependency-ledger.md](helium-dependency-ledger.md):
   - `copy`: lift the Helium patch into an Aerie directory (for example
     `patches/vendored-helium/`), keep its license header, and add it to the
     Aerie `series` before the patch that needs it. Helium is GPL-3.0; check
     that Aerie's license allows this before shipping.
   - `rewrite`: write an Aerie patch that gives the same result on plain
     ungoogled-chromium.
   - `drop`: remove the Aerie patch or the part of it that needs the Helium patch.

   Rows in the "unmodified build" table that Aerie wants to keep (branding,
   Helium's features) are copied or rewritten the same way. Anything not wanted
   is dropped.
6. **Verify.** Run the tests, then a full `./aerie build`, then check the
   browser starts and makes no connections to Google hosts.

Without the ledger, step 5 means reading all 351 Helium patches. This is why
[CONTRIBUTING.md](../CONTRIBUTING.md) makes updating it part of every pull
request that depends on a Helium patch.
