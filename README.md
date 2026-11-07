# Aerie

A Google-free, hardened Chromium browser built on top of
[Helium](https://github.com/imputnet/helium). At this stage the repo builds
**unmodified Helium** on Linux; Aerie's own patches come later.

## Layout

| Path | What it is |
| --- | --- |
| `helium-chromium/` | Submodule: Helium core ([imputnet/helium](https://github.com/imputnet/helium)) — patches, build utilities, `flags.gn`, `downloads.ini`. Same name and place as in Helium's own platform repos. |
| `helium-linux/` | Submodule: Helium's Linux platform repo ([imputnet/helium-linux](https://github.com/imputnet/helium-linux)) — Linux patches, `flags.linux.gn`, Linux tarball layer. |
| `helium.lock` | The pinned Helium release, both submodule commits, and the sha256 of every download. |
| `aerie` | The one entry point: `./aerie build`, `./aerie prune`, `./aerie info`. |
| `tools/aerie_build/` | The build tooling (Python 3.11+, standard library only). |
| `tools/requirements.txt` | Hash-pinned Python packages for Helium's utilities (Pillow), installed into the build root. |
| `tests/` | Unit tests for the build tooling. |

Pinned release: **Helium 0.21.1.1** (Chromium 157.0.8103.38) —
`helium-linux` at tag `0.21.1.1` (`4da5fdc8`), `helium-chromium` at tag
`0.21.1` (`ecb36f09`).

## Building on Linux

### Prerequisites

- x86_64 Linux. Tested on Ubuntu 24.04, 12 cores, 30 GB RAM.
- `git`, `tar`, GNU `patch`, `zstd`, `curl`, and Python 3.11 or newer
  (with the `venv` module) on `PATH`.
- About **150–200 GB** free on the drive that holds the build root, plus up
  to 30 GB more for the compiler cache.
- Network access to `tarballs.imput.net`, `github.com` and `pypi.org`.

Nothing else is installed system-wide. Helium's prepared Chromium tarballs
already carry the toolchain (clang, lld, rust, gn, siso, node) and a Debian
sysroot, so no `-dev` packages are needed. `./aerie build` installs
[ccache](https://ccache.dev) 4.14.2 and a Python venv with Pillow into the
build root, each checked against a pinned sha256.

You do not need Docker. (Upstream `helium-linux` builds in a Docker image;
Aerie builds on the host so that no image layers land on the system disk.)

### Build

```sh
git clone https://github.com/pedrosousa13/aerie.git
cd aerie
git submodule update --init helium-chromium helium-linux
./aerie build
```

Do not use `--recursive`: `helium-linux` has its own copy of
`helium-chromium` as a nested submodule, which Aerie does not use.

The browser ends up at
`<build root>/src/helium-<version>/out/Default/helium`; `./aerie info` prints
the exact path. Run it with `<that path>`.

A clean build on a 12-core / 30 GB machine took **BUILD_TIME_PLACEHOLDER**
(see "Measured build" below). An interrupted build resumes: rerun
`./aerie build`. Use `./aerie build --clean` to start the source tree over.

Options:

| Setting | Flag | Environment | Default |
| --- | --- | --- | --- |
| Build root | `--build-root DIR` | `AERIE_BUILD_ROOT` | `/mnt/linux2/aerie-build` |
| Compiler cache cap | `--ccache-size SIZE` | `AERIE_CCACHE_SIZE` | `30G` |
| Parallel jobs | `-j N` | — | autoninja's choice |

The build root must be an absolute path outside the repo checkout.

### What lives in the build root

```
<build root>/
  downloads/   tarballs and their published .hashes files
  src/helium-<version>/          patched Chromium source
  src/helium-<version>/out/Default/   build output (the browser)
  ccache/      compiler cache, capped by ccache.conf max_size
  toolchain/   ccache binary, Python venv
  home/ tmp/ xdg/ pycache/   HOME, TMPDIR, XDG_* and Python bytecode
                             for every build process
  logs/        one log per ./aerie build run
```

Every child process runs with `HOME`, `TMPDIR`, `XDG_*_HOME`,
`XDG_RUNTIME_DIR`, `PIP_CACHE_DIR`, `PYTHONPYCACHEPREFIX` and `CCACHE_DIR`
pointing into the build root, so the build writes nothing outside it except
generated icons in `helium-chromium/resources/generated/` (ignored by git).

### Source verification

Each download is checked twice before it is unpacked:

1. Helium's `downloads.py` checks it against the digest its publisher hosts
   (for the Chromium tarballs, the `.hashes` file on the
   [chromium-tarballs](https://github.com/imputnet/chromium-tarballs)
   GitHub release).
2. Aerie checks it against the sha256 pinned in `helium.lock`.

The build stops if a file has no pin, is missing, or does not match. A file
that does not match is renamed to `*.rejected`, so the next run downloads it
again. Helium does not publish signatures for these tarballs, only checksums.

### Prune

```sh
./aerie prune --dry-run   # list what would go
./aerie prune
```

This deletes downloads not pinned in `helium.lock` (and `*.partial` /
`*.rejected` files), source trees for other Helium versions, and old tool
versions, then runs `ccache --cleanup` to bring the cache under its cap.
The current tree, pinned tarballs and the compiler cache stay.

### Tests

```sh
python3 -m unittest discover -s tests -t . -v
```

Standard library only; runs in well under a second.

### Updating Helium

1. Move both submodules to the new release (`helium-linux` to its tag,
   `helium-chromium` to the matching core tag).
2. Update `helium.lock`: versions, both commits, and the sha256 of every
   tarball (from the chromium-tarballs release `SHA256SUMS`) and every
   `deps.ini` entry.
3. Run the tests: they fail if the lock and the submodules disagree.
4. `./aerie build`, then `./aerie prune` to drop the old tree.

### Measured build

MEASURED_PLACEHOLDER
