# The release build

`./aerie build --release` builds the browser the way Chromium builds a shipping browser, in its own
output directory. The default `./aerie build` stays the fast developer build in `out/Default`. Benchmark
numbers (docs/bench.md) are only meaningful on the release build.

## GN arguments

The release build adds these arguments after Helium's own `flags.gn` and `flags.linux.gn` (GN uses the last
assignment). A pytest test (`tests/test_release_build.py`) reads this table and compares it with
`tools/aerie_build/release.py`, so the table and the code cannot drift.

| Argument | Value |
| --- | --- |
| `is_official_build` | `true` |
| `chrome_pgo_phase` | `2` |
| `pgo_data_path` | `"<tree>/chrome/build/pgo_profiles/<profile>"` |
| `use_thin_lto` | `true` |
| `thin_lto_enable_optimizations` | `true` |
| `v8_enable_builtins_optimization` | `true` |

`<tree>` is the source tree and `<profile>` is the file name pinned in `helium.lock` under `[pgo.linux]`.
The output directory is `out/Release` in the same source tree.

## What Helium does with PGO

Helium turns PGO off in its own `flags.gn` (`chrome_pgo_phase=0`), so `./aerie build` does not use the profile.
Helium's `build.sh --pgo` turns it on, but only on its git-clone path, where a gclient hook downloads the profile.
Aerie uses the prepared tarballs instead, and the `chromium-<version>-linux-x64.tar.zst` tarball already holds
the profile Chromium names in `chrome/build/linux.pgo.txt`, in `chrome/build/pgo_profiles/`. That tarball is
pinned by sha256 in `helium.lock`. The release build therefore needs no download of its own, not even Chromium's
`tools/update_pgo_profiles.py` (it would need `gsutil`): `pgo_data_path` points at the file, so that script
is never run.

## The pinned profile

`helium.lock` records the profile file name and its sha256 (`[pgo.linux]`). Before `gn gen`, the release build
refuses to go on when:

- the file is not in `chrome/build/pgo_profiles/`,
- its sha256 differs from the pin, or
- `chrome/build/linux.pgo.txt` names another file than the pin (Chromium would then use a profile that was never reviewed).

When Chromium moves, `./aerie bump` brings a new tarball and a new `linux.pgo.txt`; update `[pgo.linux]` with the
new name and the output of `sha256sum` on the file, in the same change.

## ThinLTO and memory

GN refuses an explicit `concurrent_links` with ThinLTO. Chromium's `get_concurrent_links.py` computes it from
RAM (30 GB per ThinLTO link on Linux, 10 GB reserved) and gives 1 link at a time on a 30 GB machine. The link
itself runs `--thinlto-jobs=all`; there is no GN argument for a lower number.

## Build time and disk

Measured on the build machine (12 cores, 30 GB RAM, ccache warm from the developer build) on 2026-11-20, a first
`./aerie build --release` in a tree that already had the source and `out/Default`:

- Build time: 2h23m (compile 2h22m, 78,446 steps).
- Memory: peak 16.8 GB used system-wide, sampled every 10 s. One ThinLTO link at a time, no out-of-memory.
- Disk: `out/Release` is 21 GB (`out/Default` is 35 GB); the volume grew by 20.7 GB, ThinLTO cache included.
  The ThinLTO cache is capped by Chromium at 40 GB.

A build from an empty tree needs the 150 GB the developer build needs, plus this. ccache helps only the
non-LTO objects, so a second release build after a small change is much faster than the first (the ThinLTO link
runs again every time).

## The sandbox on Ubuntu

The AppArmor profile in the README must name `out/Release` as well as `out/Default`, or the release browser stops
with "No usable sandbox!". The Bench then needs `--no-sandbox` (bench.md, "Sandbox"). Do not record a baseline from
such a run.
